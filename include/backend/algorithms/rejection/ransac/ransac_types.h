// include/backend/algorithms/rejection/ransac/ransac_types.h
#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>

namespace RansacNs {

    // Paramètres utilisés par le prétraitement et l'algorithme.
    struct RansacParameters {
        int minimumMatchCount = 4;
        double reprojectionThreshold = 3.0;
        std::size_t maximumIterationCount = 2000;
        double confidence = 0.99;
        std::size_t refinementIterationCount = 10;
        int minimumInlierCount = 6;
    };

    // Données reçues avant le prétraitement.
    //
    // On cherche une transformation B -> A :
    //   sourcePoints      = points de B
    //   destinationPoints = points correspondants dans A
    struct RansacInput {
        const std::vector<cv::Point2f>& sourcePoints;
        const std::vector<cv::Point2f>& destinationPoints;
    };

    // Données après prétraitement.
    //
    // originalIndices permet de retrouver la position du match dans le tableau DataNs::Matches d'origine.
    struct RansacPreparedInput {
        std::vector<cv::Point2f> sourcePoints;
        std::vector<cv::Point2f> destinationPoints;
        std::vector<std::size_t> originalIndices;
        std::size_t originalCount = 0;
    };

    // Résultat produit par l'algorithme RANSAC.
    struct RansacOutput {
        cv::Mat transformBToA;
        std::vector<uchar> inlierMask;
        int inlierCount = 0;
    };

}
