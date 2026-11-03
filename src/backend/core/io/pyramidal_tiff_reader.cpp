// src/backend/core/io/pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"

#include <opencv2/core/hal/interface.h>
#include <tiff.h>
#include <tiffio.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace IONs {

    namespace {

        constexpr uint16_t BITS_PER_SAMPLE_8 = 8;
        constexpr uint16_t BITS_PER_SAMPLE_16 = 16;
        constexpr uint16_t CHANNEL_COUNT_GRAY = 1;
        constexpr uint16_t CHANNEL_COUNT_RGB = 3;
        constexpr uint16_t CHANNEL_COUNT_RGBA = 4;

        struct ImageMetadata {
            int width = 0;
            int height = 0;
            int type = 0;
        };

        struct TileMetadata {
            uint32_t width = 0;
            uint32_t height = 0;
            tmsize_t size = 0;
            size_t requiredBytes = 0;
            size_t pixelSize = 0;
        };

        struct RegionReadContext {
            TIFF* tiffFile = nullptr;
            cv::Rect region;
            TileMetadata tile;
            cv::Mat& output;
            std::vector<std::uint8_t>& tileBuffer;
            std::string& errorMessage;
        };

        void copyTilePixels(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            uint32_t tileOriginX,
            uint32_t tileOriginY
        );

        bool selectLevel(TIFF* tiffFile, int level, std::string& errorMessage) {
            if (!TIFFSetDirectory(tiffFile, 0)) {
                errorMessage = "cannot select TIFF base directory";
                return false;
            }
            if (level == 0) return true;

            uint16_t subIfdCount = 0;
            toff_t* subIfdOffsets = nullptr;
            if (!TIFFGetField(tiffFile, TIFFTAG_SUBIFD, &subIfdCount, &subIfdOffsets) ||
                level > static_cast<int>(subIfdCount) ||
                !TIFFSetSubDirectory(tiffFile, subIfdOffsets[level - 1])) {
                errorMessage = "requested pyramid level does not exist";
                return false;
            }
            return true;
        }

        bool isSupportedMetadata(
            uint32_t tiffWidth,
            uint32_t tiffHeight,
            uint16_t samplesPerPixel,
            uint16_t bitsPerSample,
            uint16_t planarConfig,
            uint16_t sampleFormat
        ) {
            const bool DIMENSIONS_VALID =
                tiffWidth > 0 && tiffHeight > 0 &&
                tiffWidth <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
                tiffHeight <= static_cast<uint32_t>(std::numeric_limits<int>::max());
            const bool BITS_VALID =
                bitsPerSample == BITS_PER_SAMPLE_8 || bitsPerSample == BITS_PER_SAMPLE_16;
            const bool CHANNELS_VALID =
                samplesPerPixel == CHANNEL_COUNT_GRAY ||
                samplesPerPixel == CHANNEL_COUNT_RGB ||
                samplesPerPixel == CHANNEL_COUNT_RGBA;
            return DIMENSIONS_VALID && BITS_VALID && CHANNELS_VALID &&
                   planarConfig == PLANARCONFIG_CONTIG && sampleFormat == SAMPLEFORMAT_UINT;
        }

        bool getImageMetadata(TIFF* tiffFile, ImageMetadata& metadata, std::string& errorMessage) {
            uint32_t tiffWidth = 0;
            uint32_t tiffHeight = 0;
            uint16_t samplesPerPixel = CHANNEL_COUNT_GRAY;
            uint16_t bitsPerSample = 0;
            uint16_t planarConfig = PLANARCONFIG_CONTIG;
            uint16_t sampleFormat = SAMPLEFORMAT_UINT;
            if (!TIFFGetField(tiffFile, TIFFTAG_IMAGEWIDTH, &tiffWidth) ||
                !TIFFGetField(tiffFile, TIFFTAG_IMAGELENGTH, &tiffHeight) ||
                !TIFFGetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel) ||
                !TIFFGetField(tiffFile, TIFFTAG_BITSPERSAMPLE, &bitsPerSample) ||
                !TIFFGetField(tiffFile, TIFFTAG_PLANARCONFIG, &planarConfig)) {
                errorMessage = "cannot read TIFF image metadata";
                return false;
            }

            TIFFGetField(tiffFile, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
            if (!isSupportedMetadata(
                    tiffWidth, tiffHeight, samplesPerPixel, bitsPerSample, planarConfig, sampleFormat)) {
                errorMessage = "unsupported TIFF image format";
                return false;
            }

            const int DEPTH = bitsPerSample == BITS_PER_SAMPLE_8 ? CV_8U : CV_16U;
            metadata.width = static_cast<int>(tiffWidth);
            metadata.height = static_cast<int>(tiffHeight);
            metadata.type = CV_MAKETYPE(DEPTH, samplesPerPixel);
            return true;
        }

        bool getTileMetadata(TIFF* tiffFile, int imageType, TileMetadata& metadata, std::string& errorMessage) {
            if (!TIFFGetField(tiffFile, TIFFTAG_TILEWIDTH, &metadata.width) ||
                !TIFFGetField(tiffFile, TIFFTAG_TILELENGTH, &metadata.height) ||
                metadata.width == 0 || metadata.height == 0 ||
                metadata.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                metadata.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                errorMessage = "TIFF level is not tiled";
                return false;
            }

            metadata.size = TIFFTileSize(tiffFile);
            if (metadata.size <= 0) {
                errorMessage = "cannot determine TIFF tile size";
                return false;
            }
            metadata.pixelSize = CV_ELEM_SIZE(imageType);
            metadata.requiredBytes =
                static_cast<size_t>(metadata.width) * metadata.height * metadata.pixelSize;
            if (metadata.requiredBytes > static_cast<size_t>(metadata.size)) {
                errorMessage = "TIFF tile buffer is smaller than the declared tile";
                return false;
            }
            return true;
        }

        bool readTile(
            RegionReadContext& context,
            int tileX,
            int tileY
        ) {
            const uint32_t TILE_ORIGIN_X = static_cast<uint32_t>(tileX) * context.tile.width;
            const uint32_t TILE_ORIGIN_Y = static_cast<uint32_t>(tileY) * context.tile.height;
            const ttile_t TILE_INDEX = TIFFComputeTile(
                context.tiffFile, TILE_ORIGIN_X, TILE_ORIGIN_Y, 0, 0);
            const tmsize_t DECODED_BYTES = TIFFReadEncodedTile(
                context.tiffFile, TILE_INDEX, context.tileBuffer.data(), context.tile.size);
            if (DECODED_BYTES < static_cast<tmsize_t>(context.tile.requiredBytes)) {
                context.errorMessage = "cannot read TIFF tile";
                return false;
            }

            const cv::Rect TILE_RECT{
                static_cast<int>(TILE_ORIGIN_X),
                static_cast<int>(TILE_ORIGIN_Y),
                static_cast<int>(context.tile.width),
                static_cast<int>(context.tile.height)
            };
            const cv::Rect COPIED_RECT = TILE_RECT & context.region;
            copyTilePixels(context, COPIED_RECT, TILE_ORIGIN_X, TILE_ORIGIN_Y);
            return true;
        }

        void copyTilePixels(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            uint32_t tileOriginX,
            uint32_t tileOriginY
        ) {
            for (int y = copiedRect.y; y < copiedRect.y + copiedRect.height; ++y) {
                const size_t SOURCE_OFFSET =
                    (static_cast<size_t>(y) - tileOriginY) * context.tile.width * context.tile.pixelSize +
                    (static_cast<size_t>(copiedRect.x) - tileOriginX) * context.tile.pixelSize;
                const size_t DESTINATION_OFFSET =
                    static_cast<size_t>(y - context.region.y) * context.output.step +
                    static_cast<size_t>(copiedRect.x - context.region.x) * context.tile.pixelSize;
                std::memcpy(
                    context.output.data + DESTINATION_OFFSET,
                    context.tileBuffer.data() + SOURCE_OFFSET,
                    static_cast<size_t>(copiedRect.width) * context.tile.pixelSize
                );
            }
        }

        bool readTiles(RegionReadContext& context) {
            const int FIRST_TILE_X =
                context.region.x / static_cast<int>(context.tile.width);
            const int LAST_TILE_X =
                (context.region.x + context.region.width - 1) / static_cast<int>(context.tile.width);
            const int FIRST_TILE_Y =
                context.region.y / static_cast<int>(context.tile.height);
            const int LAST_TILE_Y =
                (context.region.y + context.region.height - 1) / static_cast<int>(context.tile.height);

            for (int tileY = FIRST_TILE_Y; tileY <= LAST_TILE_Y; ++tileY) {
                for (int tileX = FIRST_TILE_X; tileX <= LAST_TILE_X; ++tileX) {
                    if (!readTile(context, tileX, tileY)) return false;
                }
            }
            return true;
        }

    }

    PyramidalTiffReaderClass::PyramidalTiffReaderClass(std::filesystem::path path)
        : tiffPath(std::move(path)) {}

    bool PyramidalTiffReaderClass::readRegion(
        int level,
        const cv::Rect& region,
        cv::Mat& output,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        output.release();
        if (level < 0) {
            errorMessage = "pyramid level must be non-negative";
            return false;
        }
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0) {
            errorMessage = "region must have positive dimensions and non-negative coordinates";
            return false;
        }

        TIFF* tiffFile = TIFFOpen(tiffPath.string().c_str(), "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + tiffPath.string();
            return false;
        }

        bool success = false;
        do {
            ImageMetadata image;
            if (!selectLevel(tiffFile, level, errorMessage) ||
                !getImageMetadata(tiffFile, image, errorMessage)) break;
            if (region.x > image.width - region.width || region.y > image.height - region.height) {
                errorMessage = "region is outside the requested pyramid level";
                break;
            }

            TileMetadata tile;
            if (!getTileMetadata(tiffFile, image.type, tile, errorMessage)) break;
            output.create(region.height, region.width, image.type);
            std::vector<std::uint8_t> tileBuffer(static_cast<size_t>(tile.size));
            RegionReadContext context{tiffFile, region, tile, output, tileBuffer, errorMessage};
            success = readTiles(context);
        } while (false);

        TIFFClose(tiffFile);
        if (!success) output.release();
        return success;
    }

}
