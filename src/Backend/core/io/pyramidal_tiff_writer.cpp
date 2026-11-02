// src/Backend/core/io/pyramidal_tiff_writer.cpp
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

        constexpr uint32_t TILE_SIZE = static_cast<uint32_t>(DataNs::BLOCK_SIZE);
        constexpr int PYRAMID_LEVELS = DataNs::LEVEL_COUNT;

        // Configure les métadonnées / tags du niveau courant de la pyramide (est donc exécuté pour chaque niveau)
        bool setDirectoryFields(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            const int depth = levelImage.depth();
            const int channels = levelImage.channels();
            const int bitsPerSample = depth == CV_8U ? 8 : depth == CV_16U ? 16 : 0;

            // Vérification que cv::Mat est bien codé sur 8 ou 16 bits et qu'elle possède 1, 3 ou 4 canaux (1 = niveau de gris, 3 = RGB, 4 = RGBA)
            if (bitsPerSample == 0 || (channels != 1 && channels != 3 && channels != 4)) {
                errorMessage = "only CV_8U/CV_16U images with 1, 3, or 4 channels are supported";
                return false;
            }

            // TIFFSetField permet de configurer les tags du répertoire TIFF courant -> renvoie 1 (succès) ou 0 (échec)
            // On configure :
            //  - TIFFTAG_IMAGEWIDTH & TIFFTAG_IMAGELENGTH : dimensions de l'image ;
            //  - TIFFTAG_SAMPLESPERPIXEL : nombre de canaux ;
            //  - TIFFTAG_BITSPERSAMPLE : nombre de bits par canal ;
            //  - TIFFTAG_ORIENTATION : l'origine de l'image ;
            //  - TIFFTAG_PLANARCONFIG : l'entrelacement des données en mémoire ;
            //  - TIFFTAG_PHOTOMETRIC : le modèle colorimétrique ;
            //  - TIFFTAG_COMPRESSION : le type de compression ;
            //  - TIFFTAG_TILEWIDTH & TIFFTAG_TILELENGTH : dimensions des tuiles ;
            const uint16_t photometric =
                channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(levelImage.cols)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(levelImage.rows)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(channels)) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bitsPerSample)) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) || // ORIENTATION_TOPLEFT = origine en haut à gauche
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) || // PLANARCONFIG_CONTIG = "RGBRGB et non RRRGGGBBB"
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, TILE_SIZE) || // Les blocs de 512 x 512 pixels
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, TILE_SIZE)) {
                errorMessage = "cannot configure TIFF directory";
                return false;
            }

            // Gestion du canal alpha -> si l'image en possède 4 canaux, on configure le tag TIFFTAG_EXTRASAMPLES pour indiquer que le 4ème canal est un canal alpha (EXTRASAMPLE_UNASSALPHA)
            if (channels == 4 &&
                !TIFFSetField(tiffFile, TIFFTAG_EXTRASAMPLES, 1, static_cast<uint16_t>(EXTRASAMPLE_UNASSALPHA))) {
                errorMessage = "cannot configure TIFF alpha channel";
                return false;
            }

            return true;
        }

        // Découpage et écriture en tuiles
        bool writeLevel(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            // Calcul du poids en octets d'une tuile standard (512 x 512 x le nombre d'octets par pixel de l'image) pour le niveau courant de la pyramide
            const size_t tileBytes = static_cast<size_t>(TILE_SIZE) * TILE_SIZE * levelImage.elemSize();
            // Image noire de 512 x 512 pixels -> sert de buffer
            cv::Mat tileBuffer = cv::Mat::zeros(
                static_cast<int>(TILE_SIZE),
                static_cast<int>(TILE_SIZE),
                levelImage.type()
            );

            // Parcours en grille -> de haut en bas et de gauche à droite en faisant des pas de 512 pixels
            for (int pixelY = 0; pixelY < levelImage.rows; pixelY += static_cast<int>(TILE_SIZE)) {
                for (int pixelX = 0; pixelX < levelImage.cols; pixelX += static_cast<int>(TILE_SIZE)) {
                    // On remet "à zéro" le buffer avant de gérer la nouvelle tuile
                    tileBuffer.setTo(cv::Scalar::all(0));
                    // Calcul de la zone exacte à copier
                    const cv::Rect sourceRect{
                        pixelX,
                        pixelY,
                        std::min(static_cast<int>(TILE_SIZE), levelImage.cols - pixelX),
                        std::min(static_cast<int>(TILE_SIZE), levelImage.rows - pixelY)
                    };
                    // Copie du morceau
                    levelImage(sourceRect).copyTo(tileBuffer(cv::Rect{
                        0, 0, sourceRect.width, sourceRect.height
                    }));

                    // Écriture dans le fichier TIFF
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
        // Vérification
        errorMessage.clear();
        if (image.empty()) {
            errorMessage = "input image is empty";
            return false;
        }
        if (image.dims != 2 || image.cols <= 0 || image.rows <= 0) {
            errorMessage = "input image must be a non-empty two-dimensional matrix";
            return false;
        }

        // Dossier de sortie
        std::error_code directoryError;
        const auto parentDirectory = outputPath.parent_path();
        if (!parentDirectory.empty()) {
            std::filesystem::create_directories(parentDirectory, directoryError);
            if (directoryError) {
                errorMessage = "cannot create output directory: " + directoryError.message();
                return false;
            }
        }

        // Ouverture du fichier TIFF en mode écriture
        TIFF* tiffFile = TIFFOpen(outputPath.string().c_str(), "w");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open output TIFF";
            return false;
        }

        // Écriture pour la gestion des erreurs -> on break si une erreur durant le process survient
        bool success = false;
        do {
            // TIFFTAG_SUBIFD = tag pour indiquer les sous-répertoires pour les niveaux de la pyramide
            toff_t subIfdOffsets[PYRAMID_LEVELS - 1] = {};
            if (!TIFFSetField(tiffFile, TIFFTAG_SUBIFD, PYRAMID_LEVELS - 1, subIfdOffsets)) {
                errorMessage = "cannot configure TIFF pyramid directories";
                break;
            }

            // Écriture du niveau 0 (l'image originale) -> si une des fonctions échoue, on break
            if (!setDirectoryFields(tiffFile, image, errorMessage) || !writeLevel(tiffFile, image, errorMessage)) {
                break;
            }

            cv::Mat previousLevel = image;
            // On boucle sur tous les (sous-) niveaux de la pyramide (1 à 3)
            for (int levelIndex = 1; levelIndex < PYRAMID_LEVELS; ++levelIndex) {
                cv::Mat reducedLevel;
                // On réduit l'image précédente de moitié pour créer le niveau suivant de la pyramide
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

        // Nettoyage -> ferme le pointeur de fichier (TIFFClose). En cas d'erreur durant n'importe quelle étape de la boucle, le fichier corrompu/incomplet est automatiquement supprimé du disque.
        TIFFClose(tiffFile);
        if (!success) {
            std::error_code removeError;
            std::filesystem::remove(outputPath, removeError);
        }
        return success;
    }

}
