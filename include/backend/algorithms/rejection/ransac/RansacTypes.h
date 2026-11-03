#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>

namespace stitching::rejection::ransac {

// Paramètres utilisés par le prétraitement et l'algorithme.
struct RansacParameters {
    int minMatches = 4;

    double reprojectionThreshold = 3.0;

    std::size_t maxIters = 2000;

    double confidence = 0.99;

    std::size_t refineIters = 10;

    int minInliers = 6;
};

// Données reçues avant le prétraitement.
//
// On cherche une transformation B -> A.
//
// sourcePoints      = points de B
// destinationPoints = points correspondants dans A
struct RansacInput {
    const std::vector<cv::Point2f>& sourcePoints;
    const std::vector<cv::Point2f>& destinationPoints;
};

// Données après prétraitement.
//
// originalIndices permet de retrouver la position
// du match dans le tableau Matches d'origine.
struct RansacPreparedInput {
    std::vector<cv::Point2f> sourcePoints;
    std::vector<cv::Point2f> destinationPoints;

    std::vector<std::size_t> originalIndices;

    std::size_t originalCount = 0;
};

// Résultat produit par l'algorithme RANSAC.
struct RansacOutput {
    cv::Mat transform;

    std::vector<uchar> inliers;

    int inlierCount = 0;
};

} // namespace stitching::rejection::ransac