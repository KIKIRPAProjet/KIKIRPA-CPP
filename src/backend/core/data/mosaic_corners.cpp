// src/backend/core/data/mosaic_corners.cpp
#include "core/data/mosaic_corners.h"

#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <limits>
#include <vector>

namespace DataNs {

    // Le minimum est soustrait tel quel, sans être arrondi vers le bas. Avec floor, un minimum de -12,005 donnait un
    // décalage de -13 : la tuile la plus à gauche tombait en 0,995, arrondi à 1, la colonne 0 de la mosaïque restait
    // vide et la toile avait un pixel de trop. Ici cette tuile est en 0,0 exactement avant l'arrondi (x - x vaut 0 en
    // flottant), donc en 0. Les autres coins gardent une erreur d'arrondi d'au plus un demi-pixel.
    // Soustraire puis arrondir, dans cet ordre : un seul arrondi par coin.
    std::vector<cv::Point> computeMosaicCorners(const std::vector<cv::Point2d>& positions) {
        double minimumX = std::numeric_limits<double>::infinity();
        double minimumY = std::numeric_limits<double>::infinity();
        for (const cv::Point2d& position : positions) {
            minimumX = std::min(minimumX, position.x);
            minimumY = std::min(minimumY, position.y);
        }
        std::vector<cv::Point> corners;
        corners.reserve(positions.size());
        for (const cv::Point2d& position : positions)
            corners.emplace_back(cvRound(position.x - minimumX), cvRound(position.y - minimumY));
        return corners;
    }

}
