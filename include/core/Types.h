#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

enum class Direction { BottomRight, LeftRight };

// Contexte d'une paire de tuiles voisines, transmis aux algorithmes
struct PairContext {
    Direction dir = Direction::LeftRight;
    float overlap = 0.f;   // recouvrement estimé (fraction de la tuile)
};

// Sortie d'un FeatureMatcher : pointsA[i] <-> pointsB[i]
struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;

    std::vector<float> scores;

    float dx = 0.0f;
    float dy = 0.0f;
};

// Sortie d'un OutlierRejector
struct Alignment {
    cv::Mat transform;            // transformation b -> a (3x3, CV_64F)
    std::vector<uchar> inliers;   // 1 = inlier, même taille que Matches
};

} // namespace stitching
