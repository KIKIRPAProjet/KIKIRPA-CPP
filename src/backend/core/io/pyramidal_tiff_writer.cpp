// src/backend/core/io/pyramidal_tiff_writer.cpp
#include "core/io/pyramidal_tiff_writer.h"

#include "core/data/tile.h"
#include "core/io/unicode_path.h"
#include "core/io/file_replacement.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace IONs {

    namespace {

        // Mêmes valeurs que la pyramide décrite par TileClass : les dimensions de blocs doivent concorder.
        constexpr uint32_t TILE_SIZE = static_cast<uint32_t>(DataNs::BLOCK_SIZE);
        constexpr int PYRAMID_LEVELS = DataNs::LEVEL_COUNT;
        constexpr int BITS_PER_BYTE = 8;
        // Chaque niveau est le précédent réduit de moitié, en largeur comme en hauteur.
        constexpr double REDUCTION_FACTOR = 0.5;

        // Formats gérés : 8 ou 16 bits par canal ; 1 canal (gris), 3 (RGB) ou 4 (RGBA).
        bool isSupportedLevel(const cv::Mat& levelImage) {
            const int depth = levelImage.depth();
            const int channels = levelImage.channels();
            return (depth == CV_8U || depth == CV_16U) && (channels == 1 || channels == 3 || channels == 4);
        }

        // Dimensions, canaux, bits par canal, origine en haut à gauche, canaux entrelacés (RGBRGB et non RRRGGGBBB),
        // modèle colorimétrique, compression LZW et taille des blocs. TIFFSetField renvoie 0 en cas d'échec.
        bool setBaseFields(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            const auto channels = static_cast<uint16_t>(levelImage.channels());
            const auto bitsPerSample = static_cast<uint16_t>(levelImage.elemSize1() * BITS_PER_BYTE);
            const uint16_t photometric = channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(levelImage.cols)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(levelImage.rows)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, channels) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, bitsPerSample) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, TILE_SIZE) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, TILE_SIZE)) {
                errorMessage = "cannot configure TIFF directory";
                return false;
            }
            return true;
        }

        // Avec 4 canaux, le dernier est un canal alpha non prémultiplié.
        // TIFFTAG_EXTRASAMPLES attend un nombre et un tableau (pointeur), pas la valeur elle-même.
        bool setAlphaField(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            if (levelImage.channels() != 4) return true;
            const uint16_t extraSamples[] = {EXTRASAMPLE_UNASSALPHA};
            if (!TIFFSetField(tiffFile, TIFFTAG_EXTRASAMPLES, 1, extraSamples)) {
                errorMessage = "cannot configure TIFF alpha channel";
                return false;
            }
            return true;
        }

        // Embarque le profil ICC, s'il y en a un.
        bool setIccProfileField(TIFF* tiffFile, const std::vector<std::uint8_t>& iccProfile, std::string& errorMessage) {
            if (iccProfile.empty()) return true;
            if (!TIFFSetField(tiffFile, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(iccProfile.size()), iccProfile.data())) {
                errorMessage = "cannot configure TIFF ICC profile";
                return false;
            }
            return true;
        }

        // Configure les tags du répertoire TIFF courant (un par niveau de la pyramide).
        bool setDirectoryFields(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage,
                                const std::vector<std::uint8_t>& iccProfile = {}) {
            if (!isSupportedLevel(levelImage)) {
                errorMessage = "only CV_8U/CV_16U images with 1, 3, or 4 channels are supported";
                return false;
            }
            return setBaseFields(tiffFile, levelImage, errorMessage) &&
                   setAlphaField(tiffFile, levelImage, errorMessage) &&
                   setIccProfileField(tiffFile, iccProfile, errorMessage);
        }

        // Ce qu'il faut pour écrire les blocs d'un niveau : le fichier, l'image du niveau et un bloc complet réutilisé.
        struct LevelWriteContext {
            TIFF* tiffFile;
            const cv::Mat& levelImage;
            // TIFF exige des blocs complets : ceux du bord sont donc complétés par du noir.
            cv::Mat tileBuffer;
        };

        // Copie dans le buffer le bloc dont le coin haut-gauche est `origin` (noir au-delà de l'image), puis l'écrit.
        bool writeTile(LevelWriteContext& context, cv::Point origin) {
            const cv::Mat& levelImage = context.levelImage;
            context.tileBuffer.setTo(cv::Scalar::all(0));
            // Zone à copier : un bloc entier, ou moins sur les bords droit et bas de l'image.
            const cv::Rect sourceRect{
                origin.x,
                origin.y,
                std::min(static_cast<int>(TILE_SIZE), levelImage.cols - origin.x),
                std::min(static_cast<int>(TILE_SIZE), levelImage.rows - origin.y)
            };
            levelImage(sourceRect).copyTo(context.tileBuffer(cv::Rect{0, 0, sourceRect.width, sourceRect.height}));

            // TIFF numérote les blocs d'un niveau ligne par ligne ; on lui demande le numéro du bloc contenant ce pixel.
            const ttile_t tileIndex = TIFFComputeTile(
                context.tiffFile, static_cast<uint32_t>(origin.x), static_cast<uint32_t>(origin.y), 0, 0);
            const auto tileBytes = static_cast<tmsize_t>(context.tileBuffer.total() * context.tileBuffer.elemSize());
            return TIFFWriteEncodedTile(context.tiffFile, tileIndex, context.tileBuffer.data, tileBytes) != -1;
        }

        // Écrit une rangée de blocs, de gauche à droite.
        bool writeTileRow(LevelWriteContext& context, int pixelY, std::string& errorMessage) {
            for (int pixelX = 0; pixelX < context.levelImage.cols; pixelX += static_cast<int>(TILE_SIZE)) {
                if (!writeTile(context, cv::Point{pixelX, pixelY})) {
                    errorMessage = "cannot write TIFF tile";
                    return false;
                }
            }
            return true;
        }

        // Écrit le niveau en blocs de TILE_SIZE x TILE_SIZE, puis clôt le répertoire TIFF.
        bool writeLevel(TIFF* tiffFile, const cv::Mat& levelImage, std::string& errorMessage) {
            LevelWriteContext context{
                tiffFile,
                levelImage,
                cv::Mat::zeros(static_cast<int>(TILE_SIZE), static_cast<int>(TILE_SIZE), levelImage.type())
            };

            // Parcours en grille : de haut en bas, par pas d'un bloc.
            for (int pixelY = 0; pixelY < levelImage.rows; pixelY += static_cast<int>(TILE_SIZE)) {
                if (!writeTileRow(context, pixelY, errorMessage)) return false;
            }

            if (!TIFFWriteDirectory(tiffFile)) {
                errorMessage = "cannot finalize TIFF directory";
                return false;
            }
            return true;
        }

        // Écrit les niveaux 1 à PYRAMID_LEVELS-1 : chacun est le précédent réduit de moitié.
        bool writeReducedLevels(TIFF* tiffFile, const cv::Mat& image, std::string& errorMessage) {
            cv::Mat previousLevel = image;
            for (int levelIndex = 1; levelIndex < PYRAMID_LEVELS; ++levelIndex) {
                cv::Mat reducedLevel;
                // INTER_AREA moyenne les pixels : la meilleure interpolation pour réduire une image.
                cv::resize(previousLevel, reducedLevel, cv::Size(), REDUCTION_FACTOR, REDUCTION_FACTOR, cv::INTER_AREA);
                if (reducedLevel.empty()) {
                    errorMessage = "cannot create reduced pyramid level";
                    return false;
                }
                if (!setDirectoryFields(tiffFile, reducedLevel, errorMessage) ||
                    !writeLevel(tiffFile, reducedLevel, errorMessage)) {
                    return false;
                }
                previousLevel = std::move(reducedLevel);
            }
            return true;
        }

        // Écrit toute la pyramide : réserve les sous-répertoires (SubIFD) des niveaux réduits, puis le niveau 0 (l'image
        // d'origine, avec le profil ICC) et enfin les niveaux réduits.
        bool writePyramid(TIFF* tiffFile, const cv::Mat& image, std::string& errorMessage,
                          const std::vector<std::uint8_t>& iccProfile) {
            toff_t subIfdOffsets[PYRAMID_LEVELS - 1] = {};
            if (!TIFFSetField(tiffFile, TIFFTAG_SUBIFD, PYRAMID_LEVELS - 1, subIfdOffsets)) {
                errorMessage = "cannot configure TIFF pyramid directories";
                return false;
            }
            return setDirectoryFields(tiffFile, image, errorMessage, iccProfile) &&
                   writeLevel(tiffFile, image, errorMessage) &&
                   writeReducedLevels(tiffFile, image, errorMessage);
        }

        bool validateInputImage(const cv::Mat& image, std::string& errorMessage) {
            if (image.empty()) {
                errorMessage = "input image is empty";
                return false;
            }
            if (image.dims != 2 || image.cols <= 0 || image.rows <= 0) {
                errorMessage = "input image must be a non-empty two-dimensional matrix";
                return false;
            }
            return true;
        }

        bool createOutputDirectory(const std::filesystem::path& outputPath, std::string& errorMessage) {
            const auto parentDirectory = outputPath.parent_path();
            if (parentDirectory.empty()) return true;
            std::error_code directoryError;
            std::filesystem::create_directories(parentDirectory, directoryError);
            if (directoryError) {
                errorMessage = "cannot create output directory: " + directoryError.message();
                return false;
            }
            return true;
        }

    }

    bool createPyramidalTiledTiff(
        const cv::Mat& image,
        const std::filesystem::path& outputPath,
        std::string& errorMessage,
        const std::vector<std::uint8_t>& iccProfile
    ) {
        // 1. Contrôle de l'image ; 2. création du dossier et ouverture du TIFF ; 3. écriture des niveaux ; 4. fermeture,
        // et suppression du fichier si une étape a échoué.
        errorMessage.clear();
        if (!validateInputImage(image, errorMessage) || !createOutputDirectory(outputPath, errorMessage)) return false;

        // Écriture dans un fichier temporaire unique pour éviter tout conflit concurrent
        const auto tempPath = IONs::makeTemporaryPath(outputPath);
        TIFF* tiffFile = IONs::openTiff(tempPath, "w");
        if (tiffFile == nullptr) {
            errorMessage = "cannot open output TIFF: " + tempPath.string();
            return false;
        }

        const bool isWritten = writePyramid(tiffFile, image, errorMessage, iccProfile);
        TIFFClose(tiffFile);

        // Publication atomique : on remplace outputPath par tempPath. Si l'écriture ou la publication a échoué, on
        // détruit le temporaire et on sort.
        if (!isWritten || !IONs::replaceFile(tempPath, outputPath, errorMessage)) {
            std::error_code removeError;
            std::filesystem::remove(tempPath, removeError);
            return false;
        }

        return true;
    }

}
