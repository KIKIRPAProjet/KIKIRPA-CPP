#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

enum class Direction { BottomRight, LeftRight };

// Contexte d'une paire de tuiles voisines, transmis aux algorithmes
struct PairContext {
    Direction dir = Direction::LeftRight;
    float overlap = 0.f;   // recouvrement estimé (fraction de la tuile)
};

// Sortie d'un FeatureMatcher : pointsA[i] <-> pointsB[i].
// Les scores sont optionnels et correspondent aux paires de points par indice.
struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;
    std::vector<float> scores;
    float dx = 0.f; // translation médiane estimée de B vers A
    float dy = 0.f;
};

// Sortie d'un OutlierRejector
struct Alignment {
    cv::Mat transform;            // transformation b -> a (3x3, CV_64F)
    std::vector<uchar> inliers;   // 1 = inlier, même taille que Matches
};

struct TilePositionData {
    std::string tile_id;
    cv::Point2d position;
};

} // namespace stitching
