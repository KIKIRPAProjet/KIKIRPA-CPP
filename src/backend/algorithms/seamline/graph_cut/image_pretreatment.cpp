// src/backend/algorithms/seamline/graph_cut/image_pretreatment.cpp
#include "algorithms/seamline/graph_cut/image_pretreatment.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/types.hpp> // Pour cv::Size, cv::Point
#include <opencv2/core/mat.hpp>   // Pour cv::Mat, cv::UMat
#include <opencv2/core.hpp>
#include <opencv2/core/fast_math.hpp>     // Pour cvRound, cvFloor
#include <opencv2/core/hal/interface.h>   // Pour CV_8U, CV_16UC3, CV_32FC3...
#include <opencv2/core/saturate.hpp>      // Pour cv::saturate_cast
#include <tiffio.h>
#include <tiff.h>
#include "core/data/algorithm_types.h"
#include "core/data/mosaic_corners.h"
#include "core/io/unicode_path.h"
#include <cstddef>
#include <cstdio>
#include <string>
#include <regex>
#include <vector>
#include <memory>
#include <cstdint>
#include <utility>


namespace GraphCutNs {

    constexpr double SCALE_8BIT_TO_16BIT = 257.0; // 255 -> 65535
    constexpr double SCALE_16S_TO_16U = 2.0;
    constexpr double MAX_16BIT_VAL = 65535.0;
    constexpr double SCALE_16BIT_TO_FLOAT = 255.0 / 65535.0;
    constexpr int BUFFER_SIZE = 32;
    constexpr int MASK_FILLED = 255;

    namespace {

        // Uniformise une image en BGR à trois canaux, en conservant la profondeur.
        // La transparence est supprimée, les niveaux de gris sont dupliqués sur trois canaux.
        // Pour les images à deux canaux, seul le premier est conservé.
        cv::Mat toThreeChannels(const cv::Mat& rawImage) {
            if (rawImage.channels() == 4) {
                cv::Mat bgr;
                cv::cvtColor(rawImage, bgr, cv::COLOR_BGRA2BGR);
                return bgr;
            }
            cv::Mat singleChannel = rawImage;
            if (rawImage.channels() == 2) {
                std::vector<cv::Mat> channels;
                cv::split(rawImage, channels);
                singleChannel = channels[0];
            }
            if (singleChannel.channels() == 1) {
                cv::Mat bgr;
                cv::cvtColor(singleChannel, bgr, cv::COLOR_GRAY2BGR);
                return bgr;
            }
            return singleChannel;
        }

        // Convertit les valeurs en entiers non signés sur 16 bits.
        // Le facteur 257 transforme exactement la valeur 255 en 65535.
        // Les valeurs signées sur 16 bits sont doublées, les valeurs négatives deviennent nulles.
        // Le cas par défaut suppose des valeurs flottantes comprises entre 0 et 1.
        cv::Mat toUnsigned16Bit(const cv::Mat& image) {
            cv::Mat image16Bit;
            switch (image.depth()) {
                case CV_16U: image16Bit = image; break;
                case CV_8U:  image.convertTo(image16Bit, CV_16UC3, SCALE_8BIT_TO_16BIT); break;     // 255 -> 65535
                case CV_16S: image.convertTo(image16Bit, CV_16UC3, SCALE_16S_TO_16U); break;
                default:     image.convertTo(image16Bit, CV_16UC3, MAX_16BIT_VAL); break;   // flottant 0–1
            }
            return image16Bit;
        }

    }

    // Charge une image et la convertit en BGR à trois canaux, en entiers non signés sur 16 bits.
    // La transparence est supprimée, les niveaux de gris sont dupliqués sur trois canaux.
    // Renvoie une image vide si la lecture échoue.
    cv::Mat ImagePretreatmentClass::read16Bit(const std::string& path) {
        cv::Mat rawImage = IONs::readImage(IONs::pathFromUtf8(path), cv::IMREAD_UNCHANGED);
        if (rawImage.empty()) return rawImage;
        return toUnsigned16Bit(toThreeChannels(rawImage));
    }

    // Prépare une image réduite à trois canaux flottants pour le calcul de la ligne de raccord.
    // Le facteur fourni doit respecter : 0 < scale <= 1.
    //
    // Fichier 16 bits : la réduction est faite dès la lecture, puis l'image pleine résolution est libérée avant toute
    // autre conversion (canaux, flottant), qui ne portent plus que sur l'image réduite. Ces opérations s'appliquent canal
    // par canal : le résultat est strictement identique à celui d'une conversion faite avant la réduction.
    // Autres profondeurs (8 bits, 16 bits signé, flottant) : la conversion en 16 bits non signé est faite AVANT la
    // réduction, comme à l'origine. Réduire d'abord en 8 bits arrondirait les valeurs interpolées à l'entier et décalerait
    // légèrement les coûts, donc, mesuré sur des tuiles de test, des pixels de la mosaïque changeraient de tuile.
    cv::Mat ImagePretreatmentClass::readSmallFloat(const std::string& path, double scale) {
        cv::Mat image = IONs::readImage(IONs::pathFromUtf8(path), cv::IMREAD_UNCHANGED);
        if (image.empty()) return image;

        if (image.depth() != CV_16U)
            image = toUnsigned16Bit(toThreeChannels(image));   // l'image d'origine est libérée à l'affectation

        if (scale < 1.0) {
            cv::Mat smallImage;
            cv::resize(image, smallImage, cv::Size(), scale, scale, cv::INTER_LINEAR);
            image = std::move(smallImage);   // l'image pleine résolution n'a plus de référence : mémoire libérée ici
        }

        cv::Mat floatImage;
        toUnsigned16Bit(toThreeChannels(image)).convertTo(floatImage, CV_32FC3, SCALE_16BIT_TO_FLOAT);
        return floatImage;
    }

    // Extrait la colonne et la ligne d'un nom se terminant par _C<nombre>_R<nombre>.tif ou .tiff, comme les images lues par
    // le step import (format par défaut img_C(\d+)_R(\d+)\.tif : colonne d'abord, puis ligne).
    // Construit un identifiant tel que "r03_c07" (ligne, colonne). Sert seulement au positionnement autonome
    // (PositioningNs::computeTilePositions), qui part de chemins de fichiers : le pipeline, lui, identifie les tuiles
    // par TileClass::getId().
    // Renvoie false si le nom ne correspond pas au format attendu.
    bool ImagePretreatmentClass::parseTileName(const std::string& path, int& row, int& column, std::string& tileId) {
        static const std::regex pattern("_C(\\d+)_R(\\d+)\\.tiff?$", std::regex::icase);

        // Conserver uniquement le nom du fichier, sans son répertoire
        std::string fileName = path;
        const size_t lastSlash = fileName.find_last_of("/\\");
        if (lastSlash != std::string::npos) fileName = fileName.substr(lastSlash + 1);

        std::smatch matchGroups;
        if (!std::regex_search(fileName, matchGroups, pattern) || matchGroups.size() <= 2) return false;
        column = std::stoi(matchGroups[1].str());
        row = std::stoi(matchGroups[2].str());

        // Utiliser au moins deux chiffres par coordonnée pour former l'identifiant
        char buffer[BUFFER_SIZE];
        std::snprintf(buffer, sizeof(buffer), "r%02d_c%02d", row, column);
        tileId = buffer;
        return true;
    }

    namespace {

        // Lit les dimensions d'une image à partir de l'en-tête du fichier, sans décoder les pixels.
        // Pour un TIFF, seuls les champs IMAGEWIDTH / IMAGELENGTH de la première image du fichier sont lus, soit la même
        // image que celle renvoyée par cv::imread. Si le fichier n'est pas un TIFF lisible, on retombe sur une lecture
        // complète (comportement d'origine). Renvoie false si l'image est illisible ou vide.
        bool readImageSize(const std::string& imagePath, cv::Size& size) {
            using TiffHandle = std::unique_ptr<TIFF, decltype(&TIFFClose)>;
            const TiffHandle tiffFile(IONs::openTiff(IONs::pathFromUtf8(imagePath), "r"), &TIFFClose);
            if (tiffFile) {
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                if (TIFFGetField(tiffFile.get(), TIFFTAG_IMAGEWIDTH, &width) &&
                    TIFFGetField(tiffFile.get(), TIFFTAG_IMAGELENGTH, &height) && width > 0 && height > 0) {
                    size = cv::Size(static_cast<int>(width), static_cast<int>(height));
                    return true;
                }
            }
            const cv::Mat image = ImagePretreatmentClass::read16Bit(imagePath);
            if (image.empty()) return false;
            size = image.size();
            return true;
        }

        // Lit la taille d'une tuile et ajoute son masque réduit à `tiles`. Seul l'en-tête du fichier est lu : l'image
        // pleine résolution n'est pas décodée. Renvoie false si elle est illisible ou vide une fois réduite (cause dans
        // errorMessage).
        bool addTileSizeAndMask(const std::string& imagePath, double scale, PreprocessedTiles& tiles, std::string& errorMessage) {
            cv::Size fullSize;
            if (!readImageSize(imagePath, fullSize)) {
                errorMessage = "Impossible to read tile: " + imagePath;
                return false;
            }
            const cv::Size smallSize(cv::saturate_cast<int>(fullSize.width * scale),
                                     cv::saturate_cast<int>(fullSize.height * scale));
            if (smallSize.width < 1 || smallSize.height < 1) {
                errorMessage = "Tile is empty after reduction (" + std::to_string(fullSize.width) + "x" +
                               std::to_string(fullSize.height) + " pixels at scale " + std::to_string(scale) + "): " + imagePath;
                return false;
            }
            // Tous les pixels sont initialement utilisables. Le calcul de la ligne de raccord modifiera ensuite ces masques.
            tiles.masksUmatSmall.push_back(cv::UMat(smallSize, CV_8U, cv::Scalar(MASK_FILLED)));
            tiles.tileSizes.push_back(fullSize);
            return true;
        }

        // Convertit les positions pleine résolution vers la grille réduite.
        // Arrondit vers le bas pour obtenir des coordonnées entières sur cette grille.
        std::vector<cv::Point> toSmallCorners(const std::vector<cv::Point>& originalCorners, double scale) {
            std::vector<cv::Point> smallCorners;
            smallCorners.reserve(originalCorners.size());
            for (size_t tileIndex = 0; tileIndex < originalCorners.size(); ++tileIndex)
                smallCorners.push_back(cv::Point(cvFloor(originalCorners[tileIndex].x * scale),
                                                 cvFloor(originalCorners[tileIndex].y * scale)));
            return smallCorners;
        }

    }

    // Prépare les dimensions, les masques réduits et les positions des tuiles aux deux résolutions.
    // Les positions sont fournies par inputPositions, une par tuile dans l'ordre de tilePaths, et non calculées à
    // partir des images. En cas d'échec, la cause est dans errorMessage (voir getErrorMessage).
    int ImagePretreatmentClass::processImages(const std::vector<DataNs::TilePositionData>& inputPositions,
                                              const std::vector<std::string>& tilePaths, double scale,
                                              PreprocessedTiles& tiles) {
        errorMessage.clear();
        if (!(scale > 0.0 && scale <= 1.0)) {
            errorMessage = "Reduction scale must be greater than 0 and at most 1, got " + std::to_string(scale);
            return -1;
        }
        if (inputPositions.size() != tilePaths.size()) {
            errorMessage = "The number of positions (" + std::to_string(inputPositions.size()) +
                           ") doesn't match the number of tiles (" + std::to_string(tilePaths.size()) + ")";
            return -1;
        }
        std::vector<cv::Point2d> rawCorners;
        for (std::size_t tileIndex = 0; tileIndex < tilePaths.size(); ++tileIndex) {
            if (!addTileSizeAndMask(tilePaths[tileIndex], scale, tiles, errorMessage)) return -1;
            rawCorners.push_back(inputPositions[tileIndex].position);
        }
        if (tiles.tileSizes.empty()) {
            errorMessage = "No tile provided for image pretreatment";
            return -1;
        }
        // Repère partagé avec fusion : les territoires et les pixels de la mosaïque tombent ainsi au même endroit.
        tiles.originalCorners = DataNs::computeMosaicCorners(rawCorners);
        tiles.cornersSmall = toSmallCorners(tiles.originalCorners, scale);
        return 1;
    }

}
