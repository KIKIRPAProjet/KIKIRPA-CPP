// src/backend/core/io/pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"

#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace IONs {

    namespace {

        // Sélectionne le répertoire TIFF correspondant au niveau demandé.
        bool selectLevel(TIFF* tiffFile, int level, std::string& errorMessage) {
            // La fonction n'écrit pas la valeur de SubFID dans une variable mais modifie directement l'état interne du pointeur TIFF* tiffFile
            // Toutes les fonctions libtiff appelées par la suite vont travailler exclusivement sur le répertoire sélectionné.
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

        // Vérifie le format du niveau et le convertit en type OpenCV.
        bool getImageType(TIFF* tiffFile, int& width, int& height, int& type, std::string& errorMessage) {
            uint32_t tiffWidth = 0;
            uint32_t tiffHeight = 0;
            uint16_t samplesPerPixel = 1;
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
            if (tiffWidth == 0 || tiffHeight == 0 ||
                tiffWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                tiffHeight > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                (bitsPerSample != 8 && bitsPerSample != 16) ||
                (samplesPerPixel != 1 && samplesPerPixel != 3 && samplesPerPixel != 4) ||
                planarConfig != PLANARCONFIG_CONTIG ||
                sampleFormat != SAMPLEFORMAT_UINT) {
                errorMessage = "unsupported TIFF image format";
                return false;
            }

            const int depth = bitsPerSample == 8 ? CV_8U : CV_16U;
            width = static_cast<int>(tiffWidth);
            height = static_cast<int>(tiffHeight);
            type = CV_MAKETYPE(depth, samplesPerPixel);
            return true;
        }

    }

    PyramidalTiffReader::PyramidalTiffReader(std::filesystem::path path) : tiffPath(std::move(path)) {}

    bool PyramidalTiffReader::readRegion(
        int level,
        const cv::Rect& region,
        cv::Mat& output,
        std::string& errorMessage
    ) const {
        // Pré-vérifications et initialisation
        errorMessage.clear();
        output.release();
        if (level < 0) {
            errorMessage = "pyramid level must be non-negative";
            return false;
        }
        // Exclusion des régions ou dimensions négatives
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0) {
            errorMessage = "region must have positive dimensions and non-negative coordinates";
            return false;
        }

        // On tente d'ouvrir le fichier TIFF et de sélectionner le niveau demandé.
        TIFF* tiffFile = TIFFOpen(tiffPath.string().c_str(), "r");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open TIFF: " + tiffPath.string();
            return false;
        }

        bool success = false;
        do {
            // Sélection du niveau demandé.
            if (!selectLevel(tiffFile, level, errorMessage)) break;

            int imageWidth = 0;
            int imageHeight = 0;
            int imageType = 0;
            // Lecture du type d'image et des dimensions du niveau sélectionné.
            if (!getImageType(tiffFile, imageWidth, imageHeight, imageType, errorMessage)) break;
            // Les coordonnées de la région sont toujours celles du niveau sélectionné ! On vérifie donc qu'elles sont valides pour ce niveau.
            if (region.x > imageWidth - region.width || region.y > imageHeight - region.height) {
                errorMessage = "region is outside the requested pyramid level";
                break;
            }

            // Vérification du tuilage
            uint32_t tileWidth = 0;
            uint32_t tileHeight = 0;
            if (!TIFFGetField(tiffFile, TIFFTAG_TILEWIDTH, &tileWidth) ||
                !TIFFGetField(tiffFile, TIFFTAG_TILELENGTH, &tileHeight) ||
                tileWidth == 0 || tileHeight == 0 ||
                tileWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                tileHeight > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                errorMessage = "TIFF level is not tiled";
                break;
            }

            const tmsize_t tileSize = TIFFTileSize(tiffFile);
            if (tileSize <= 0) {
                errorMessage = "cannot determine TIFF tile size";
                break;
            }
            const size_t requiredTileBytes =
                static_cast<size_t>(tileWidth) * tileHeight * CV_ELEM_SIZE(imageType);
            if (requiredTileBytes > static_cast<size_t>(tileSize)) {
                errorMessage = "TIFF tile buffer is smaller than the declared tile";
                break;
            }

            // Allocation de la matrice de sortie et du tampon (tileBuffer) de lecture des tuiles.
            output.create(region.height, region.width, imageType);
            std::vector<std::uint8_t> tileBuffer(static_cast<size_t>(tileSize));
            const int firstTileX = region.x / static_cast<int>(tileWidth);
            const int lastTileX = (region.x + region.width - 1) / static_cast<int>(tileWidth);
            const int firstTileY = region.y / static_cast<int>(tileHeight);
            const int lastTileY = (region.y + region.height - 1) / static_cast<int>(tileHeight);
            const size_t pixelSize = CV_ELEM_SIZE(imageType);

            // Lit uniquement les tuiles qui recouvrent la région demandée.
            for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
                for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
                    const uint32_t tileOriginX = static_cast<uint32_t>(tileX) * tileWidth;
                    const uint32_t tileOriginY = static_cast<uint32_t>(tileY) * tileHeight;
                    // Calcul de l'index de la tuile
                    const ttile_t tileIndex = TIFFComputeTile(tiffFile, tileOriginX, tileOriginY, 0, 0);
                    // Ajout de la lecture de la tuile dans le tampon tileBuffer
                    const tmsize_t decodedBytes = TIFFReadEncodedTile(
                        tiffFile, tileIndex, tileBuffer.data(), tileSize);
                    if (decodedBytes < static_cast<tmsize_t>(requiredTileBytes)) {
                        errorMessage = "cannot read TIFF tile";
                        output.release();
                        break;
                    }

                    // Calcul de l'intersection entre la tuile en cours et la région demandée
                    const cv::Rect tileRect{
                        static_cast<int>(tileOriginX),
                        static_cast<int>(tileOriginY),
                        static_cast<int>(tileWidth),
                        static_cast<int>(tileHeight)
                    };
                    const cv::Rect copiedRect = tileRect & region;
                    // Ignore les pixels de la tuile hors de la région demandée
                    for (int y = copiedRect.y; y < copiedRect.y + copiedRect.height; ++y) {
                        // Calcule où lire dans le tileBuffer et où écrire dans la matrice de sortie
                        const size_t sourceOffset =
                            (static_cast<size_t>(y) - tileOriginY) * tileWidth * pixelSize +
                            (static_cast<size_t>(copiedRect.x) - tileOriginX) * pixelSize;
                        const size_t destinationOffset =
                            static_cast<size_t>(y - region.y) * output.step +
                            static_cast<size_t>(copiedRect.x - region.x) * pixelSize;


                        // Copie des pixels vers la matrice de sortie
                        std::memcpy(
                            output.data + destinationOffset,
                            tileBuffer.data() + sourceOffset,
                            static_cast<size_t>(copiedRect.width) * pixelSize
                        );
                    }
                }
                if (!errorMessage.empty()) break;
            }
            success = errorMessage.empty();
        } while (false);

        // Fermeture propre du pointeur du fichier
        TIFFClose(tiffFile);
        return success;
    }

}
