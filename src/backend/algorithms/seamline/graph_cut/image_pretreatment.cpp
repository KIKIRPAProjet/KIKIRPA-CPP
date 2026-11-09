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
#include <algorithm>
#include "core/data/algorithm_types.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <map>
#include <regex>
#include <vector>
#include <limits>


namespace GraphCutNs {

    // Charge une image et la convertit en BGR à trois canaux, en entiers non signés sur 16 bits.
    // La transparence est supprimée, les niveaux de gris sont dupliqués sur trois canaux.
    // Renvoie une image vide si la lecture échoue.
    
    constexpr double SCALE_8BIT_TO_16BIT = 257.0; // 255 -> 65535
    constexpr double SCALE_16S_TO_16U = 2.0;
    constexpr double MAX_16BIT_VAL = 65535.0;
    constexpr double SCALE_16BIT_TO_FLOAT = 255.0 / 65535.0;
    constexpr int BUFFER_SIZE = 32;
    constexpr int MASK_FILLED = 255;
    cv::Mat ImagePretreatmentClass::read16Bit(const std::string& path) {
        cv::Mat rawImage = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (rawImage.empty()) return rawImage;
        // Uniformiser les canaux avant de convertir les valeurs des pixels.
        // Pour les images à deux canaux, seul le premier est conservé.
        if (rawImage.channels() == 4) {
            cv::cvtColor(rawImage, rawImage, cv::COLOR_BGRA2BGR);
        } else if (rawImage.channels() == 2) {
            std::vector<cv::Mat> channels;
            cv::split(rawImage, channels);
            rawImage = channels[0];
        }
        if (rawImage.channels() == 1) cv::cvtColor(rawImage, rawImage, cv::COLOR_GRAY2BGR);

        // Convertir les valeurs en entiers non signés sur 16 bits.
        // Le facteur 257 transforme exactement la valeur 255 en 65535.
        // Les valeurs signées sur 16 bits sont doublées, les valeurs négatives deviennent nulles.
        // Le cas par défaut suppose des valeurs flottantes comprises entre 0 et 1.
        cv::Mat image16Bit;
        switch (rawImage.depth()) {
            case CV_16U: image16Bit = rawImage; break;
            case CV_8U:  rawImage.convertTo(image16Bit, CV_16UC3, SCALE_8BIT_TO_16BIT); break;     // 255 -> 65535
            case CV_16S: rawImage.convertTo(image16Bit, CV_16UC3, SCALE_16S_TO_16U); break;
            default:     rawImage.convertTo(image16Bit, CV_16UC3, MAX_16BIT_VAL); break;   // flottant 0–1
        }
        return image16Bit;
    }

    // Prépare une image réduite à trois canaux flottants pour le calcul de la ligne de raccord.
    // Le redimensionnement est effectué en 16 bits, puis les valeurs passent de [0, 65535] à [0, 255].
    // Le facteur fourni doit respecter : 0 < scale <= 1.
    cv::Mat ImagePretreatmentClass::readSmallFloat(const std::string& path, double scale) {
        cv::Mat image = read16Bit(path);
        if (image.empty()) return image;
        cv::Mat smallImage16Bit = image;

        if (scale < 1.0) cv::resize(image, smallImage16Bit, cv::Size(), scale, scale, cv::INTER_LINEAR);
        image.release();
        cv::Mat floatImage;
        smallImage16Bit.convertTo(floatImage, CV_32FC3, SCALE_16BIT_TO_FLOAT);
        return floatImage;
    }

    // Extrait la ligne et la colonne d'un nom se terminant par _r<nombre>_c<nombre>.tif ou .tiff.
    // Construit un identifiant tel que "r03_c07", utilisé pour rechercher la position.
    // Renvoie false si le nom ne correspond pas au format attendu.
    bool ImagePretreatmentClass::parseTileName(const std::string& path, int& row, int& column, std::string& tileId) {
        static const std::regex pattern("_r(\\d+)_c(\\d+)\\.tiff?$", std::regex::icase);

        // Conserver uniquement le nom du fichier, sans son répertoire
        std::string fileName = path;
        const size_t lastSlash = fileName.find_last_of("/\\");
        if (lastSlash != std::string::npos) fileName = fileName.substr(lastSlash + 1);

        std::smatch matchGroups;
        if (!std::regex_search(fileName, matchGroups, pattern) || matchGroups.size() <= 2) return false;
        row = std::stoi(matchGroups[1].str());
        column = std::stoi(matchGroups[2].str());

        // Utiliser au moins deux chiffres par coordonnée pour former l'identifiant
        char buffer[BUFFER_SIZE];
        std::snprintf(buffer, sizeof(buffer), "r%02d_c%02d", row, column);
        tileId = buffer;
        return true;
    }

    namespace {

        // Positions des tuiles, retrouvées par l'identifiant "rLL_cCC" tiré du nom de fichier.
        using PositionByTileId = std::map<std::string, cv::Point2d>;

        // Table temporaire construite à partir de la structure d'entrée, pour une recherche rapide.
        PositionByTileId indexPositionsByTileId(const std::vector<DataNs::TilePositionData>& inputPositions) {
            PositionByTileId positionByTileId;
            for (const auto& tilePosition : inputPositions)
                positionByTileId[tilePosition.tileId] = tilePosition.position;
            return positionByTileId;
        }

        // Lit la taille d'une tuile et ajoute son masque réduit à `tiles`. L'image pleine résolution n'est conservée que
        // le temps de lire ses dimensions. Renvoie false si elle est illisible ou vide une fois réduite (cause dans
        // errorMessage).
        bool addTileSizeAndMask(const std::string& imagePath, double scale, PreprocessedTiles& tiles, std::string& errorMessage) {
            const cv::Mat image = ImagePretreatmentClass::read16Bit(imagePath);
            if (image.empty()) {
                errorMessage = "Impossible to read tile: " + imagePath;
                return false;
            }
            const cv::Size fullSize = image.size();
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

        // Associe le fichier à sa position grâce à l'identifiant extrait de son nom. Renvoie false si le nom n'est pas
        // reconnu ou si la position manque (cause dans errorMessage).
        bool findRawCorner(const PositionByTileId& positionByTileId, const std::string& imagePath, cv::Point2d& rawCorner,
                           std::string& errorMessage) {
            int row = 0;
            int column = 0;
            std::string tileId;
            if (!ImagePretreatmentClass::parseTileName(imagePath, row, column, tileId)) {
                errorMessage = "Tile file name must end with _r<row>_c<column>.tif or .tiff: " + imagePath;
                return false;
            }
            const auto tilePosition = positionByTileId.find(tileId);
            if (tilePosition == positionByTileId.end()) {
                errorMessage = "No position provided for tile " + tileId + " (" + imagePath + ")";
                return false;
            }
            rawCorner = tilePosition->second;
            return true;
        }

        // Déplace les positions dans un repère de mosaïque à coordonnées positives ou nulles.
        // Soustrait les minima arrondis vers le bas, puis arrondit les positions au pixel.
        // Toutes les tuiles subissent la même translation avant cet arrondi.
        std::vector<cv::Point> toMosaicCorners(const std::vector<cv::Point2d>& rawCorners) {
            double minimumX = std::numeric_limits<double>::max();
            double minimumY = std::numeric_limits<double>::max();

            for (const cv::Point2d& corner : rawCorners) {
                minimumX = std::min(minimumX, corner.x);
                minimumY = std::min(minimumY, corner.y);
            }
            const double offsetX = std::floor(minimumX);
            const double offsetY = std::floor(minimumY);
            std::vector<cv::Point> originalCorners;
            // Réserve la taille, sinon clang-tidy est pas content
            originalCorners.reserve(rawCorner.size());
            for (const cv::Point2d& corner : rawCorners)
                originalCorners.push_back(cv::Point(cvRound(corner.x - offsetX), cvRound(corner.y - offsetY)));
            return originalCorners;
        }

        // Convertit les positions pleine résolution vers la grille réduite.
        // Arrondit vers le bas pour obtenir des coordonnées entières sur cette grille.
        std::vector<cv::Point> toSmallCorners(const std::vector<cv::Point>& originalCorners, double scale) {
            std::vector<cv::Point> smallCorners;
            for (size_t tileIndex = 0; tileIndex < originalCorners.size(); ++tileIndex)
                smallCorners.push_back(cv::Point(cvFloor(originalCorners[tileIndex].x * scale),
                                                 cvFloor(originalCorners[tileIndex].y * scale)));
            return smallCorners;
        }

    }

    // Prépare les dimensions, les masques réduits et les positions des tuiles aux deux résolutions.
    // Les positions sont fournies par inputPositions, et non calculées à partir des images.
    // En cas d'échec, la cause est dans errorMessage (voir getErrorMessage).
    int ImagePretreatmentClass::processImages(const std::vector<DataNs::TilePositionData>& inputPositions,
                                              const std::vector<std::string>& tilePaths, double scale,
                                              PreprocessedTiles& tiles) {
        errorMessage.clear();
        if (!(scale > 0.0 && scale <= 1.0)) {
            errorMessage = "Reduction scale must be greater than 0 and at most 1, got " + std::to_string(scale);
            return -1;
        }
        const PositionByTileId positionByTileId = indexPositionsByTileId(inputPositions);
        std::vector<cv::Point2d> rawCorners;
        for (const std::string& imagePath : tilePaths) {
            cv::Point2d rawCorner;
            if (!addTileSizeAndMask(imagePath, scale, tiles, errorMessage) ||
                !findRawCorner(positionByTileId, imagePath, rawCorner, errorMessage))
                return -1;
            rawCorners.push_back(rawCorner);
        }
        if (tiles.tileSizes.empty()) {
            errorMessage = "No tile provided for image pretreatment";
            return -1;
        }
        tiles.originalCorners = toMosaicCorners(rawCorners);
        tiles.cornersSmall = toSmallCorners(tiles.originalCorners, scale);
        return 1;
    }

}
