// src/backend/core/io/pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/cvdef.h>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
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

        struct ReadRequest {
            int level = 0;
            cv::Rect region;
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

        bool readTile(
            RegionReadContext& context,
            int tileX,
            int tileY
        ) {
            const uint32_t tileOriginX = static_cast<uint32_t>(tileX) * context.tile.width;
            const uint32_t tileOriginY = static_cast<uint32_t>(tileY) * context.tile.height;
            const ttile_t tileIndex = TIFFComputeTile(
                context.tiffFile, tileOriginX, tileOriginY, 0, 0);
            const tmsize_t decodedBytes = TIFFReadEncodedTile(
                context.tiffFile, tileIndex, context.tileBuffer.data(), context.tile.size);
            if (decodedBytes < static_cast<tmsize_t>(context.tile.requiredBytes)) {
                context.errorMessage = "cannot read TIFF tile";
                return false;
            }

            const cv::Rect tileRect{
                static_cast<int>(tileOriginX),
                static_cast<int>(tileOriginY),
                static_cast<int>(context.tile.width),
                static_cast<int>(context.tile.height)
            };
            const cv::Rect copiedRect = tileRect & context.region;
            copyTilePixels(context, copiedRect, tileOriginX, tileOriginY);
            return true;
        }

        void copyTilePixels(
            const RegionReadContext& context,
            const cv::Rect& copiedRect,
            uint32_t tileOriginX,
            uint32_t tileOriginY
        ) {
            for (int y = copiedRect.y; y < copiedRect.y + copiedRect.height; ++y) {
                const size_t sourceOffset =
                    (static_cast<size_t>(y) - tileOriginY) * context.tile.width * context.tile.pixelSize +
                    (static_cast<size_t>(copiedRect.x) - tileOriginX) * context.tile.pixelSize;
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

        // Les deux fonctions qui suivent lisent une image à BANDES. Le cache, lui, est toujours tuilé : elles ne
        // servent qu'à lire une région directement dans l'image de l'œuvre, qui est l'organisation habituelle des
        // TIFF d'acquisition.

        // Chemin ordinaire : libtiff décode les bandes que la région recoupe. Coût proportionnel à la hauteur
        // d'une bande, donc inutilisable sur une image compressée d'une seule bande de plusieurs milliers de
        // lignes : c'est à l'appelant de le savoir avant de choisir ce fichier (voir la sonde du lecteur de tuile).
        bool readStrips(TIFF* tiffFile, const cv::Rect& region, const ImageMetadata& image, cv::Mat& output,
                        std::string& errorMessage) {
            uint32_t rowsPerStrip = 0;
            if (!TIFFGetField(tiffFile, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip) || rowsPerStrip == 0) {
                errorMessage = "cannot read TIFF rows per strip";
                return false;
            }
            const tmsize_t stripSize = TIFFStripSize(tiffFile);
            if (stripSize <= 0) {
                errorMessage = "cannot determine TIFF strip size";
                return false;
            }
            const size_t pixelSize = CV_ELEM_SIZE(image.type);
            const size_t imageRowBytes = static_cast<size_t>(image.width) * pixelSize;
            const size_t copiedBytes = static_cast<size_t>(region.width) * pixelSize;
            std::vector<std::uint8_t> strip(static_cast<size_t>(stripSize));

            const int stripHeight = static_cast<int>(rowsPerStrip);
            for (int firstRow = region.y; firstRow < region.y + region.height;) {
                const int stripIndex = firstRow / stripHeight;
                const int stripFirstRow = stripIndex * stripHeight;
                const tmsize_t decodedBytes = TIFFReadEncodedStrip(
                    tiffFile, static_cast<tstrip_t>(stripIndex), strip.data(), stripSize);
                if (decodedBytes <= 0) {
                    errorMessage = "cannot read TIFF strip";
                    return false;
                }
                const int lastRow = std::min(region.y + region.height, stripFirstRow + stripHeight);
                for (int row = firstRow; row < lastRow; ++row) {
                    const size_t sourceOffset = (static_cast<size_t>(row - stripFirstRow)) * imageRowBytes +
                                               static_cast<size_t>(region.x) * pixelSize;
                    if (sourceOffset + copiedBytes > static_cast<size_t>(decodedBytes)) {
                        errorMessage = "TIFF strip is shorter than its declared rows";
                        return false;
                    }
                    std::memcpy(output.ptr(row - region.y), strip.data() + sourceOffset, copiedBytes);
                }
                firstRow = lastRow;
            }
            return true;
        }

        // Une image non compressée à canaux entrelacés se lit par simple décalage : l'octet d'une ligne se calcule.
        // On lit alors exactement les lignes de la région, même quand l'image n'a qu'une seule bande de plusieurs
        // milliers de lignes — le cas des TIFF du KIKIRPA, où readStrips aurait décodé l'image entière à chaque
        // région. L'ordre des octets doit être celui de la machine, sinon les valeurs 16 bits sortiraient inversées.
        bool canReadRowsByOffset(TIFF* tiffFile) {
            uint16_t compression = COMPRESSION_NONE;
            return TIFFGetField(tiffFile, TIFFTAG_COMPRESSION, &compression) &&
                   compression == COMPRESSION_NONE && TIFFIsByteSwapped(tiffFile) == 0;
        }

        // false seulement si la lecture échoue vraiment ; `unsupported` dit que ce chemin ne s'applique pas et que
        // l'appelant doit passer par les bandes (fichier illisible en parallèle, décalages absents).
        bool readRowsByOffset(const std::filesystem::path& tiffPath, TIFF* tiffFile, const cv::Rect& region,
                              const ImageMetadata& image, cv::Mat& output, bool& unsupported,
                              std::string& errorMessage) {
            unsupported = true;
            uint32_t rowsPerStrip = 0;
            std::uint64_t* stripOffsets = nullptr;
            if (!TIFFGetField(tiffFile, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip) || rowsPerStrip == 0 ||
                !TIFFGetField(tiffFile, TIFFTAG_STRIPOFFSETS, &stripOffsets) || stripOffsets == nullptr)
                return false;
            std::ifstream stream(tiffPath, std::ios::binary);
            if (!stream) return false;   // le fichier est déjà ouvert par libtiff : si le partage le refuse, on
                                         // retombe sur les bandes au lieu d'échouer.
            unsupported = false;

            const size_t pixelSize = CV_ELEM_SIZE(image.type);
            const size_t imageRowBytes = static_cast<size_t>(image.width) * pixelSize;
            const size_t copiedBytes = static_cast<size_t>(region.width) * pixelSize;
            const int stripHeight = static_cast<int>(rowsPerStrip);
            for (int row = region.y; row < region.y + region.height; ++row) {
                const int stripIndex = row / stripHeight;
                const std::uint64_t offset = stripOffsets[stripIndex] +
                    static_cast<std::uint64_t>(row - stripIndex * stripHeight) * imageRowBytes +
                    static_cast<std::uint64_t>(region.x) * pixelSize;
                stream.seekg(static_cast<std::streamoff>(offset));
                stream.read(reinterpret_cast<char*>(output.ptr(row - region.y)),
                            static_cast<std::streamsize>(copiedBytes));
                if (!stream) {
                    errorMessage = "cannot read TIFF rows at their offset";
                    return false;
                }
            }
            return true;
        }

        bool readOpenedRegion(
            const std::filesystem::path& tiffPath,
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

            output.create(request.region.height, request.region.width, image.type);
            // Trois organisations possibles : tuilée (le cache, et certaines sources), non compressée à bandes
            // (lecture par décalage), et compressée à bandes (décodage des bandes recoupées).
            if (!TIFFIsTiled(tiffFile)) {
                if (canReadRowsByOffset(tiffFile)) {
                    bool unsupported = false;
                    if (readRowsByOffset(tiffPath, tiffFile, request.region, image, output, unsupported,
                                         errorMessage))
                        return true;
                    if (!unsupported) return false;
                }
                return readStrips(tiffFile, request.region, image, output, errorMessage);
            }

            TileMetadata tile;
            if (!getTileMetadata(tiffFile, image.type, tile, errorMessage)) return false;
            std::vector<std::uint8_t> tileBuffer(static_cast<size_t>(tile.size));
            RegionReadContext context{
                tiffFile, request.region, tile, output, tileBuffer, errorMessage};
            return readTiles(context);
        }

    }

    PyramidalTiffReaderClass::PyramidalTiffReaderClass(std::filesystem::path path)
        : tiffPath(std::move(path)) {}

    bool PyramidalTiffReaderClass::readLevel(
        int level,
        cv::Mat& output,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        output.release();
        if (level < 0) {
            errorMessage = "pyramid level must be non-negative";
            return false;
        }

        TIFF* tiffFile = IONs::openTiff(tiffPath, "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + IONs::pathToUtf8(tiffPath);
            return false;
        }

        if (!selectLevel(tiffFile, level, errorMessage)) {
            TIFFClose(tiffFile);
            return false;
        }

        ImageMetadata image;
        if (!getImageMetadata(tiffFile, image, errorMessage)) {
            TIFFClose(tiffFile);
            return false;
        }

        const ReadRequest request{level, cv::Rect(0, 0, image.width, image.height)};
        const bool success = readOpenedRegion(tiffPath, tiffFile, request, output, errorMessage);
        TIFFClose(tiffFile);
        if (!success) output.release();
        return success;
    }

    bool PyramidalTiffReaderClass::getLevelDimensions(
        int level,
        int& width,
        int& height,
        std::string& errorMessage
    ) const {
        errorMessage.clear();
        width = 0;
        height = 0;
        if (level < 0) {
            errorMessage = "pyramid level must be non-negative";
            return false;
        }

        TIFF* tiffFile = IONs::openTiff(tiffPath, "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + IONs::pathToUtf8(tiffPath);
            return false;
        }

        if (!selectLevel(tiffFile, level, errorMessage)) {
            TIFFClose(tiffFile);
            return false;
        }

        ImageMetadata image;
        const bool success = getImageMetadata(tiffFile, image, errorMessage);
        TIFFClose(tiffFile);
        if (success) {
            width = image.width;
            height = image.height;
        }
        return success;
    }

    int PyramidalTiffReaderClass::getLevelCount() const {
        TIFF* tiffFile = IONs::openTiff(tiffPath, "r");
        if (tiffFile == nullptr) return 0;
        if (!TIFFSetDirectory(tiffFile, 0)) {
            TIFFClose(tiffFile);
            return 0;
        }

        uint16_t subIfdCount = 0;
        toff_t* subIfdOffsets = nullptr;
        if (TIFFGetField(tiffFile, TIFFTAG_SUBIFD, &subIfdCount, &subIfdOffsets)) {
            TIFFClose(tiffFile);
            return 1 + static_cast<int>(subIfdCount);
        }
        TIFFClose(tiffFile);
        return 1;
    }

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

        TIFF* tiffFile = IONs::openTiff(tiffPath, "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + IONs::pathToUtf8(tiffPath);
            return false;
        }

        const ReadRequest request{level, region};
        const bool success = readOpenedRegion(tiffPath, tiffFile, request, output, errorMessage);
        TIFFClose(tiffFile);
        if (!success) output.release();
        return success;
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

        TIFF* tiffFile = IONs::openTiff(tiffPath, "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + IONs::pathToUtf8(tiffPath);
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
