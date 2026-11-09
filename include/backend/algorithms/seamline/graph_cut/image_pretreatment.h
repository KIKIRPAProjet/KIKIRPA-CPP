// include/backend/algorithms/seamline/graph_cut/image_pretreatment.h
#pragma once

#include "core/data/algorithm_types.h"

// Inclusions directes des types OpenCV pour satisfaire misc-include-cleaner
#include <opencv2/core/types.hpp> // Pour cv::Size, cv::Point
#include <opencv2/core/mat.hpp>   // Pour cv::Mat, cv::UMat
#include <string>
#include <vector>

namespace GraphCutNs {
    struct PreprocessedTiles {
        std::vector<cv::Size> tileSizes;
        std::vector<cv::UMat> masksUmatSmall;
        std::vector<cv::Point> originalCorners;
        std::vector<cv::Point> cornersSmall;
    };

    class ImagePretreatmentClass {

        public:
            // Prépare la liste des tuiles : tailles, positions (coins) et masques réduits.
            // Prend en entrée la structure DataNs::TilePositionData (sortie du positionnement) au lieu d'un fichier JSON.
            // Les vecteurs de sortie doivent être vides avant l'appel ; ils suivent l'ordre des fichiers de tilePaths.
            // Renvoie 1 en cas de succès, ou -1 lorsqu'une erreur est détectée (les vecteurs peuvent alors être
            // partiellement remplis).
            const std::string& getErrorMessage() const{ return errorMessage;}
            int processImages(const std::vector<DataNs::TilePositionData>& inputPositions, const std::vector<std::string>& tilePaths, double scale, PreprocessedTiles& tiles) ;

            // Extrait (ligne, colonne) d'un nom de fichier "..._rLL_cCC.tif(f)" et fabrique l'identifiant "rLL_cCC"
            // (2 chiffres minimum). Source unique de l'identifiant : utilisée par le positionnement (SIFT + RANSAC) ET par
            // processImages.
            static bool parseTileName(const std::string& path, int& row, int& column, std::string& tileId);

            // Relit une tuile sur le disque et renvoie sa copie réduite, en trois canaux flottants sur [0, 255]
            static cv::Mat readSmallFloat(const std::string& path, double scale);

            // Image lue sans perte en CV_16UC3
            static cv::Mat read16Bit(const std::string& path);
        private:
        std::string errorMessage;

    };

}
