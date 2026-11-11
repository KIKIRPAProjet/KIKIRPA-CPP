// src/backend/core/io/pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/cvdef.h>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
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

        struct TiffMetadata {
            uint32_t width = 0;
            uint32_t height = 0;
            uint16_t samplesPerPixel = CHANNEL_COUNT_GRAY;
            uint16_t bitsPerSample = 0;
            uint16_t planarConfig = PLANARCONFIG_CONTIG;
            uint16_t sampleFormat = SAMPLEFORMAT_UINT;
        };

        struct TileMetadata {
            uint32_t width = 0;
            uint32_t height = 0;
            tmsize_t size = 0;
            size_t requiredBytes = 0;
            size_t pixelSize = 0;
        };

        constexpr size_t BYTES_PER_KIB = 1024ULL;
        constexpr size_t BYTES_PER_MIB = BYTES_PER_KIB * BYTES_PER_KIB;
        constexpr size_t MAX_REGION_MEMORY_BYTES = 256ULL * BYTES_PER_MIB;
        constexpr size_t MAX_VIEWPORT_MEMORY_BYTES = 64ULL * BYTES_PER_MIB;

        struct ReadRequest {
            int level = 0;
            cv::Rect region;
            size_t maxMemoryBytes = MAX_REGION_MEMORY_BYTES;
            bool allowSubsampling = false;
        };

        struct ViewportBounds {
            double left = 0.0;
            double top = 0.0;
            double right = 0.0;
            double bottom = 0.0;
        };

        struct ViewportLevelRequest {
            int level = 0;
            ViewportBounds bounds;
            cv::Size fullSize;
            cv::Size levelSize;
        };

        struct RegionReadContext {
            TIFF* tiffFile = nullptr;
            cv::Rect region;
            int step = 1;
            TileMetadata tile;
            cv::Mat& output;
            std::vector<std::uint8_t>& tileBuffer;
            std::string& errorMessage;
        };

        void copyTilePixels(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            const cv::Point& tileOrigin
        );

        int calculateSubsampleStep(const cv::Rect& region, const TileMetadata& tile, size_t maxBytes) {
            const size_t requiredBytes = static_cast<size_t>(region.width) * region.height * tile.pixelSize;
            if (requiredBytes <= maxBytes || maxBytes == 0) {
                return 1;
            }
            const double ratio = static_cast<double>(requiredBytes) / static_cast<double>(maxBytes);
            return std::max(1, static_cast<int>(std::ceil(std::sqrt(ratio))));
        }

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

        bool isSupportedMetadata(const TiffMetadata& metadata) {
            const bool dimensionsValid =
                metadata.width > 0 && metadata.height > 0 &&
                metadata.width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
                metadata.height <= static_cast<uint32_t>(std::numeric_limits<int>::max());
            const bool bitsValid =
                metadata.bitsPerSample == BITS_PER_SAMPLE_8 ||
                metadata.bitsPerSample == BITS_PER_SAMPLE_16;
            const bool channelsValid =
                metadata.samplesPerPixel == CHANNEL_COUNT_GRAY ||
                metadata.samplesPerPixel == CHANNEL_COUNT_RGB ||
                metadata.samplesPerPixel == CHANNEL_COUNT_RGBA;
            return dimensionsValid && bitsValid && channelsValid &&
                   metadata.planarConfig == PLANARCONFIG_CONTIG &&
                   metadata.sampleFormat == SAMPLEFORMAT_UINT;
        }

        bool getImageMetadata(TIFF* tiffFile, ImageMetadata& metadata, std::string& errorMessage) {
            TiffMetadata tiffMetadata;
            if (!TIFFGetField(tiffFile, TIFFTAG_IMAGEWIDTH, &tiffMetadata.width) ||
                !TIFFGetField(tiffFile, TIFFTAG_IMAGELENGTH, &tiffMetadata.height) ||
                !TIFFGetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, &tiffMetadata.samplesPerPixel) ||
                !TIFFGetField(tiffFile, TIFFTAG_BITSPERSAMPLE, &tiffMetadata.bitsPerSample) ||
                !TIFFGetField(tiffFile, TIFFTAG_PLANARCONFIG, &tiffMetadata.planarConfig)) {
                errorMessage = "cannot read TIFF image metadata";
                return false;
            }

            TIFFGetField(tiffFile, TIFFTAG_SAMPLEFORMAT, &tiffMetadata.sampleFormat);
            if (!isSupportedMetadata(tiffMetadata)) {
                errorMessage = "unsupported TIFF image format";
                return false;
            }

            const int depth =
                tiffMetadata.bitsPerSample == BITS_PER_SAMPLE_8 ? CV_8U : CV_16U;
            metadata.width = static_cast<int>(tiffMetadata.width);
            metadata.height = static_cast<int>(tiffMetadata.height);
            metadata.type = CV_MAKETYPE(depth, tiffMetadata.samplesPerPixel);
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

        bool tileContainsSampledPixels(const RegionReadContext& context, const cv::Rect& copiedRect) {
            if (context.step <= 1) return true;
            const int relStartX = copiedRect.x - context.region.x;
            const int startRemX = relStartX % context.step;
            const int firstX = startRemX == 0 ? copiedRect.x : copiedRect.x + (context.step - startRemX);
            const int relStartY = copiedRect.y - context.region.y;
            const int startRemY = relStartY % context.step;
            const int firstY = startRemY == 0 ? copiedRect.y : copiedRect.y + (context.step - startRemY);
            return firstX < copiedRect.x + copiedRect.width && firstY < copiedRect.y + copiedRect.height;
        }

        bool readTile(
            RegionReadContext& context,
            int tileX,
            int tileY
        ) {
            const uint32_t tileOriginX = static_cast<uint32_t>(tileX) * context.tile.width;
            const uint32_t tileOriginY = static_cast<uint32_t>(tileY) * context.tile.height;
            const cv::Rect tileRect{
                static_cast<int>(tileOriginX),
                static_cast<int>(tileOriginY),
                static_cast<int>(context.tile.width),
                static_cast<int>(context.tile.height)
            };
            const cv::Rect copiedRect = tileRect & context.region;
            if (copiedRect.width <= 0 || copiedRect.height <= 0 ||
                !tileContainsSampledPixels(context, copiedRect)) {
                return true;
            }

            const ttile_t tileIndex = TIFFComputeTile(
                context.tiffFile, tileOriginX, tileOriginY, 0, 0);
            const tmsize_t decodedBytes = TIFFReadEncodedTile(
                context.tiffFile, tileIndex, context.tileBuffer.data(), context.tile.size);
            if (decodedBytes < static_cast<tmsize_t>(context.tile.requiredBytes)) {
                context.errorMessage = "cannot read TIFF tile";
                return false;
            }

            const cv::Point tileOrigin(
                static_cast<int>(tileOriginX), static_cast<int>(tileOriginY));
            copyTilePixels(context, copiedRect, tileOrigin);
            return true;
        }

        void copyTilePixelsFull(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            const cv::Point& tileOrigin
        ) {
            for (int y = copiedRect.y; y < copiedRect.y + copiedRect.height; ++y) {
                const size_t sourceOffset =
                    (static_cast<size_t>(y) - tileOrigin.y) * context.tile.width * context.tile.pixelSize +
                    (static_cast<size_t>(copiedRect.x) - tileOrigin.x) * context.tile.pixelSize;
                const size_t destinationOffset =
                    static_cast<size_t>(y - context.region.y) * context.output.step +
                    static_cast<size_t>(copiedRect.x - context.region.x) * context.tile.pixelSize;
                std::memcpy(
                    context.output.data + destinationOffset,
                    context.tileBuffer.data() + sourceOffset,
                    static_cast<size_t>(copiedRect.width) * context.tile.pixelSize
                );
            }
        }

        void copyTilePixelsSubsampled(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            const cv::Point& tileOrigin
        ) {
            const int step = context.step;
            const size_t pixelSize = context.tile.pixelSize;
            const int relStartX = copiedRect.x - context.region.x;
            const int startRemX = relStartX % step;
            const int firstX = startRemX == 0 ? copiedRect.x : copiedRect.x + (step - startRemX);

            const int relStartY = copiedRect.y - context.region.y;
            const int startRemY = relStartY % step;
            const int firstY = startRemY == 0 ? copiedRect.y : copiedRect.y + (step - startRemY);

            for (int y = firstY; y < copiedRect.y + copiedRect.height; y += step) {
                const int destY = (y - context.region.y) / step;
                if (destY >= context.output.rows) continue;
                uint8_t* destRow = context.output.data + static_cast<size_t>(destY) * context.output.step;
                const size_t srcRowOffset = (static_cast<size_t>(y) - tileOrigin.y) * context.tile.width * pixelSize;

                for (int x = firstX; x < copiedRect.x + copiedRect.width; x += step) {
                    const int destX = (x - context.region.x) / step;
                    if (destX >= context.output.cols) continue;
                    const size_t srcOffset = srcRowOffset + (static_cast<size_t>(x) - tileOrigin.x) * pixelSize;
                    std::memcpy(destRow + static_cast<size_t>(destX) * pixelSize,
                                context.tileBuffer.data() + srcOffset,
                                pixelSize);
                }
            }
        }

        void copyTilePixels(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            const cv::Point& tileOrigin
        ) {
            if (context.step == 1) {
                copyTilePixelsFull(context, copiedRect, tileOrigin);
            } else {
                copyTilePixelsSubsampled(context, copiedRect, tileOrigin);
            }
        }

        bool readTiles(RegionReadContext& context) {
            const int firstTileX =
                context.region.x / static_cast<int>(context.tile.width);
            const int lastTileX =
                (context.region.x + context.region.width - 1) / static_cast<int>(context.tile.width);
            const int firstTileY =
                context.region.y / static_cast<int>(context.tile.height);
            const int lastTileY =
                (context.region.y + context.region.height - 1) / static_cast<int>(context.tile.height);

            for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
                for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
                    if (!readTile(context, tileX, tileY)) return false;
                }
            }
            return true;
        }

        int determineSubsampleStep(const ReadRequest& request, const TileMetadata& tile, std::string& errorMessage) {
            const size_t requiredBytes = static_cast<size_t>(request.region.width) *
                                         request.region.height * tile.pixelSize;
            if (requiredBytes <= request.maxMemoryBytes) {
                return 1;
            }
            if (!request.allowSubsampling) {
                errorMessage = "requested region exceeds maximum memory limit";
                return 0;
            }
            return calculateSubsampleStep(request.region, tile, request.maxMemoryBytes);
        }

        bool readOpenedRegion(
            TIFF* tiffFile,
            const ReadRequest& request,
            cv::Mat& output,
            std::string& errorMessage
        ) {
            if (!selectLevel(tiffFile, request.level, errorMessage)) return false;

            ImageMetadata image;
            if (!getImageMetadata(tiffFile, image, errorMessage)) return false;
            if (request.region.x > image.width - request.region.width ||
                request.region.y > image.height - request.region.height) {
                errorMessage = "region is outside the requested pyramid level";
                return false;
            }

            TileMetadata tile;
            if (!getTileMetadata(tiffFile, image.type, tile, errorMessage)) return false;

            const int step = determineSubsampleStep(request, tile, errorMessage);
            if (step <= 0) return false;

            const int outWidth = (request.region.width + step - 1) / step;
            const int outHeight = (request.region.height + step - 1) / step;
            output.create(outHeight, outWidth, image.type);

            std::vector<std::uint8_t> tileBuffer(static_cast<size_t>(tile.size));
            RegionReadContext context{
                tiffFile, request.region, step, tile, output, tileBuffer, errorMessage};
            return readTiles(context);
        }

        bool readSubIfdDimensions(
            TIFF* tiffFile,
            std::vector<cv::Size>& dimensions,
            std::string& errorMessage
        ) {
            uint16_t subIfdCount = 0;
            toff_t* subIfdOffsets = nullptr;
            const bool hasSubIfds =
                TIFFGetField(tiffFile, TIFFTAG_SUBIFD, &subIfdCount, &subIfdOffsets) != 0;
            if (hasSubIfds && subIfdCount > 0 && subIfdOffsets == nullptr) {
                errorMessage = "cannot read TIFF pyramid offsets";
                return false;
            }

            ImageMetadata image;
            for (uint16_t index = 0; hasSubIfds && index < subIfdCount; ++index) {
                if (!selectLevel(tiffFile, static_cast<int>(index) + 1, errorMessage) ||
                    !getImageMetadata(tiffFile, image, errorMessage)) {
                    return false;
                }
                dimensions.emplace_back(image.width, image.height);
            }
            return true;
        }

        bool calculateViewportBounds(
            const PyramidViewportRequest& request,
            const cv::Size& fullSize,
            ViewportBounds& bounds,
            std::string& errorMessage
        ) {
            const double scale = request.zoomPercent / 100.0;
            const double requestedRight =
                request.origin.x + request.viewportSize.width / scale;
            const double requestedBottom =
                request.origin.y + request.viewportSize.height / scale;
            if (!std::isfinite(requestedRight) || !std::isfinite(requestedBottom)) {
                errorMessage = "viewport coordinates exceed the supported range";
                return false;
            }

            bounds.left = std::max(0.0, request.origin.x);
            bounds.top = std::max(0.0, request.origin.y);
            bounds.right = std::min(static_cast<double>(fullSize.width), requestedRight);
            bounds.bottom = std::min(static_cast<double>(fullSize.height), requestedBottom);
            if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
                errorMessage = "viewport does not intersect the TIFF image";
                return false;
            }
            return true;
        }

        int selectViewportLevel(const std::vector<cv::Size>& dimensions, double scale) {
            const cv::Size fullSize = dimensions.front();
            int selectedLevel = 0;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (int level = 0; level < static_cast<int>(dimensions.size()); ++level) {
                const cv::Size levelSize = dimensions.at(static_cast<size_t>(level));
                const double displayPixelsPerLevelPixelX =
                    scale * fullSize.width / levelSize.width;
                const double displayPixelsPerLevelPixelY =
                    scale * fullSize.height / levelSize.height;
                const double distance = std::max(
                    std::abs(std::log(displayPixelsPerLevelPixelX)),
                    std::abs(std::log(displayPixelsPerLevelPixelY)));
                if (distance < bestDistance) {
                    selectedLevel = level;
                    bestDistance = distance;
                }
            }
            return selectedLevel;
        }

        bool mapViewportRegion(
            const ViewportLevelRequest& request,
            cv::Rect& region,
            std::string& errorMessage
        ) {
            const double levelScaleX =
                static_cast<double>(request.levelSize.width) / request.fullSize.width;
            const double levelScaleY =
                static_cast<double>(request.levelSize.height) / request.fullSize.height;
            const int regionLeft =
                static_cast<int>(std::floor(request.bounds.left * levelScaleX));
            const int regionTop =
                static_cast<int>(std::floor(request.bounds.top * levelScaleY));
            const int regionRight = std::min(
                request.levelSize.width,
                static_cast<int>(std::ceil(request.bounds.right * levelScaleX)));
            const int regionBottom = std::min(
                request.levelSize.height,
                static_cast<int>(std::ceil(request.bounds.bottom * levelScaleY)));
            if (regionRight <= regionLeft || regionBottom <= regionTop) {
                errorMessage = "viewport maps to an empty pyramid region";
                return false;
            }
            region = cv::Rect(regionLeft, regionTop,
                              regionRight - regionLeft, regionBottom - regionTop);
            return true;
        }

        bool readRegionFromFile(
            const std::filesystem::path& tiffPath,
            const ReadRequest& request,
            cv::Mat& output,
            std::string& errorMessage
        ) {
            errorMessage.clear();
            output.release();
            if (request.level < 0) {
                errorMessage = "pyramid level must be non-negative";
                return false;
            }
            if (request.region.x < 0 || request.region.y < 0 ||
                request.region.width <= 0 || request.region.height <= 0) {
                errorMessage = "region must have positive dimensions and non-negative coordinates";
                return false;
            }

            TIFF* tiffFile = TIFFOpen(tiffPath.string().c_str(), "r");
            if (tiffFile == nullptr) {
                errorMessage = "cannot open TIFF: " + tiffPath.string();
                return false;
            }

            const bool success = readOpenedRegion(tiffFile, request, output, errorMessage);
            TIFFClose(tiffFile);
            if (!success) output.release();
            return success;
        }

        bool readViewportLevel(
            const std::filesystem::path& tiffPath,
            const ViewportLevelRequest& request,
            PyramidViewportRegion& output,
            std::string& errorMessage
        ) {
            if (!mapViewportRegion(request, output.region, errorMessage))
                return false;

            const double levelScaleX =
                static_cast<double>(request.levelSize.width) / request.fullSize.width;
            const double levelScaleY =
                static_cast<double>(request.levelSize.height) / request.fullSize.height;
            output.level = request.level;
            output.fullResolutionRegion = cv::Rect2d(
                output.region.x / levelScaleX, output.region.y / levelScaleY,
                output.region.width / levelScaleX, output.region.height / levelScaleY);

            const ReadRequest readRequest{
                request.level, output.region, MAX_VIEWPORT_MEMORY_BYTES, true};
            if (readRegionFromFile(tiffPath, readRequest, output.pixels, errorMessage))
                return true;

            output = {};
            return false;
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
        const ReadRequest request{level, region, MAX_REGION_MEMORY_BYTES, false};
        return readRegionFromFile(tiffPath, request, output, errorMessage);
    }

    bool PyramidalTiffReaderClass::readPyramidDimensions(
        std::vector<cv::Size>& dimensions,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        dimensions.clear();
        TIFF* tiffFile = TIFFOpen(tiffPath.string().c_str(), "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + tiffPath.string();
            return false;
        }

        if (!TIFFSetDirectory(tiffFile, 0)) {
            errorMessage = "cannot select TIFF base directory";
            TIFFClose(tiffFile);
            return false;
        }

        ImageMetadata image;
        if (!getImageMetadata(tiffFile, image, errorMessage)) {
            TIFFClose(tiffFile);
            return false;
        }
        dimensions.emplace_back(image.width, image.height);

        if (!readSubIfdDimensions(tiffFile, dimensions, errorMessage)) {
            dimensions.clear();
            TIFFClose(tiffFile);
            return false;
        }

        TIFFClose(tiffFile);
        return true;
    }

    bool PyramidalTiffReaderClass::readViewport(
        const PyramidViewportRequest& request,
        PyramidViewportRegion& output,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        output = {};

        if (request.viewportSize.width <= 0 || request.viewportSize.height <= 0 ||
            !std::isfinite(request.origin.x) || !std::isfinite(request.origin.y) ||
            !std::isfinite(request.zoomPercent) || request.zoomPercent <= 0.0) {
            errorMessage = "viewport dimensions and zoom must be positive and coordinates finite";
            return false;
        }

        std::vector<cv::Size> dimensions;
        if (!readPyramidDimensions(dimensions, errorMessage))
            return false;
        if (dimensions.empty()) {
            errorMessage = "TIFF pyramid has no image levels";
            return false;
        }
        const double scale = request.zoomPercent / 100.0;
        const cv::Size fullSize = dimensions.front();
        ViewportBounds bounds;
        if (!calculateViewportBounds(request, fullSize, bounds, errorMessage)) {
            return false;
        }

        const int selectedLevel = selectViewportLevel(dimensions, scale);
        const ViewportLevelRequest levelRequest{
            selectedLevel, bounds, fullSize,
            dimensions.at(static_cast<size_t>(selectedLevel))};
        return readViewportLevel(tiffPath, levelRequest, output, errorMessage);
    }

    bool PyramidalTiffReaderClass::readMetadata(
        std::vector<uint8_t>& iccProfile,
        double& scaleX,
        double& scaleY,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        iccProfile.clear();
        scaleX = 1.0;
        scaleY = 1.0;

        TIFF* tiffFile = TIFFOpen(tiffPath.string().c_str(), "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + tiffPath.string();
            return false;
        }

        // Pas besoin de faire de vérification d'erreur ici, car ces champs sont optionnels et peuvent ne pas exister.
        extractResolution(tiffFile, scaleX, scaleY);
        extractIccProfile(tiffFile, iccProfile);

        TIFFClose(tiffFile);
        
        return true; // On renvoie true même si ICC/Echelle sont absents, car ils sont optionnels
    }

    void PyramidalTiffReaderClass::extractResolution(TIFF* tiffFile, double& scaleX, double& scaleY) const {
        // Récupération de l'échelle
        // LibTIFF utilise généralement des floats pour la résolution
        float resX = 1.0f;
        float resY = 1.0f;
        if (TIFFGetField(tiffFile, TIFFTAG_XRESOLUTION, &resX)) {
            scaleX = static_cast<double>(resX);
        }
        if (TIFFGetField(tiffFile, TIFFTAG_YRESOLUTION, &resY)) {
            scaleY = static_cast<double>(resY);
        }
    }

    void PyramidalTiffReaderClass::extractIccProfile(TIFF* tiffFile, std::vector<uint8_t>& iccProfile) const {
        // Récupération du profil ICC
        uint32_t iccSize = 0;
        void* iccBuffer = nullptr;
        if (TIFFGetField(tiffFile, TIFFTAG_ICCPROFILE, &iccSize, &iccBuffer) && iccSize > 0 && iccBuffer != nullptr) {
            iccProfile.resize(iccSize);
            std::memcpy(iccProfile.data(), iccBuffer, iccSize);
        }
    }

}
