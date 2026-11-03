// src/backend/core/io/pyramidal_tiff_writer.cpp
#include "core/io/pyramidal_tiff_writer.h"

#include "core/data/tile.h"

#include <opencv2/imgproc.hpp>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace IONs {

    namespace {

        // Mêmes valeurs que la pyramide décrite par TileClass : les dimensions de blocs doivent concorder.
        constexpr uint32_t TILE_SIZE = static_cast<uint32_t>(DataNs::BLOCK_SIZE);
        constexpr int PYRAMID_LEVELS = DataNs::LEVEL_COUNT;

        // Configure les tags du répertoire TIFF courant (un par niveau de la pyramide). TIFFSetField renvoie 0 en cas d'échec.
        bool setDirectoryFields(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            const int depth = levelImage.depth();
            const int channels = levelImage.channels();
            const int bitsPerSample = depth == CV_8U ? 8 : depth == CV_16U ? 16 : 0;

            // Formats gérés : 8 ou 16 bits par canal ; 1 canal (gris), 3 (RGB) ou 4 (RGBA).
            if (bitsPerSample == 0 || (channels != 1 && channels != 3 && channels != 4)) {
                errorMessage = "only CV_8U/CV_16U images with 1, 3, or 4 channels are supported";
                return false;
            }

            // Dimensions, canaux, bits par canal, origine en haut à gauche, canaux entrelacés (RGBRGB et non RRRGGGBBB),
            // modèle colorimétrique, compression LZW et taille des blocs.
            const uint16_t photometric =
                channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(levelImage.cols)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(levelImage.rows)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(channels)) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bitsPerSample)) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, TILE_SIZE) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, TILE_SIZE)) {
                errorMessage = "cannot configure TIFF directory";
                return false;
            }

            // Avec 4 canaux, le dernier est un canal alpha non prémultiplié.
            if (channels == 4 &&
                !TIFFSetField(tiffFile, TIFFTAG_EXTRASAMPLES, 1, static_cast<uint16_t>(EXTRASAMPLE_UNASSALPHA))) {
                errorMessage = "cannot configure TIFF alpha channel";
                return false;
            }

            return true;
        }

        // Écrit le niveau en blocs de TILE_SIZE x TILE_SIZE, puis clôt le répertoire TIFF.
        bool writeLevel(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            const size_t tileBytes = static_cast<size_t>(TILE_SIZE) * TILE_SIZE * levelImage.elemSize();
            // Buffer d'un bloc : TIFF exige des blocs complets, ceux du bord sont donc complétés par du noir.
            cv::Mat tileBuffer = cv::Mat::zeros(
                static_cast<int>(TILE_SIZE),
                static_cast<int>(TILE_SIZE),
                levelImage.type()
            );

            // Parcours en grille : de haut en bas et de gauche à droite, par pas d'un bloc.
            for (int pixelY = 0; pixelY < levelImage.rows; pixelY += static_cast<int>(TILE_SIZE)) {
                for (int pixelX = 0; pixelX < levelImage.cols; pixelX += static_cast<int>(TILE_SIZE)) {
                    tileBuffer.setTo(cv::Scalar::all(0));
                    // Zone à copier : un bloc entier, ou moins sur les bords droit et bas de l'image.
                    const cv::Rect sourceRect{
                        pixelX,
                        pixelY,
                        std::min(static_cast<int>(TILE_SIZE), levelImage.cols - pixelX),
                        std::min(static_cast<int>(TILE_SIZE), levelImage.rows - pixelY)
                    };
                    levelImage(sourceRect).copyTo(tileBuffer(cv::Rect{
                        0, 0, sourceRect.width, sourceRect.height
                    }));

                    // TIFF numérote les blocs d'un niveau ligne par ligne ; on lui demande le numéro du bloc contenant ce pixel.
                    const ttile_t tileIndex = TIFFComputeTile(
                        tiffFile, static_cast<uint32_t>(pixelX), static_cast<uint32_t>(pixelY), 0, 0);
                    if (TIFFWriteEncodedTile(
                            tiffFile, tileIndex, tileBuffer.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                        errorMessage = "cannot write TIFF tile";
                        return false;
                    }
                }
            }

            if (!TIFFWriteDirectory(tiffFile)) {
                errorMessage = "cannot finalize TIFF directory";
                return false;
            }
            return true;
        }

    }

    bool createPyramidalTiledTiff(
        const cv::Mat& image,
        const std::filesystem::path& outputPath,
        std::string& errorMessage
    ) {
        // 1. Contrôle de l'image ; 2. création du dossier et ouverture du TIFF ; 3. écriture des niveaux ; 4. fermeture,
        // et suppression du fichier si une étape a échoué.
        errorMessage.clear();
        if (image.empty()) {
            errorMessage = "input image is empty";
            return false;
        }
        if (image.dims != 2 || image.cols <= 0 || image.rows <= 0) {
            errorMessage = "input image must be a non-empty two-dimensional matrix";
            return false;
        }

        std::error_code directoryError;
        const auto parentDirectory = outputPath.parent_path();
        if (!parentDirectory.empty()) {
            std::filesystem::create_directories(parentDirectory, directoryError);
            if (directoryError) {
                errorMessage = "cannot create output directory: " + directoryError.message();
                return false;
            }
        }

        TIFF* tiffFile = TIFFOpen(outputPath.string().c_str(), "w");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open output TIFF";
            return false;
        }

        // do/while(false) : un seul point de sortie, pour fermer et supprimer le fichier en cas d'erreur (break).
        bool success = false;
        do {
            // Réserve les sous-répertoires (SubIFD) qui contiendront les niveaux réduits.
            toff_t subIfdOffsets[PYRAMID_LEVELS - 1] = {};
            if (!TIFFSetField(tiffFile, TIFFTAG_SUBIFD, PYRAMID_LEVELS - 1, subIfdOffsets)) {
                errorMessage = "cannot configure TIFF pyramid directories";
                break;
            }

            // Niveau 0 : l'image d'origine.
            if (!setDirectoryFields(tiffFile, image, errorMessage) || !writeLevel(tiffFile, image, errorMessage)) {
                break;
            }

            // Niveaux 1 à PYRAMID_LEVELS-1 : chacun est le précédent réduit de moitié.
            cv::Mat previousLevel = image;
            for (int levelIndex = 1; levelIndex < PYRAMID_LEVELS; ++levelIndex) {
                cv::Mat reducedLevel;
                // INTER_AREA moyenne les pixels : la meilleure interpolation pour réduire une image.
                cv::resize(previousLevel, reducedLevel, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
                if (reducedLevel.empty()) {
                    errorMessage = "cannot create reduced pyramid level";
                    break;
                }
                if (!setDirectoryFields(tiffFile, reducedLevel, errorMessage) || !writeLevel(tiffFile, reducedLevel, errorMessage)) {
                    break;
                }
                previousLevel = std::move(reducedLevel);
            }

            success = errorMessage.empty();
        } while (false);

        TIFFClose(tiffFile);
        if (!success) {
            std::error_code removeError;
            std::filesystem::remove(outputPath, removeError);   // pas de fichier incomplet laissé sur le disque
        }
        return success;
    }

}
