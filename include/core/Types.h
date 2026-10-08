#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

// Position de la tuile B par rapport à la tuile A
enum class Direction { Top, Bottom, Left, Right };

// Contexte d'une paire de tuiles voisines, transmis aux algorithmes
struct PairContext {
    Direction dir = Direction::Right;
    float overlap = 0.f;   // recouvrement estimé (fraction de la tuile)
};

// Sortie d'un FeatureMatcher : pointsA[i] <-> pointsB[i]
struct Matches {
    float x = 0.f, y = 0.f;                     // coin haut-gauche de B dans le repère de A (px)
    std::vector<cv::Point2f> pointsA, pointsB;  // coordonnées dans la tuile A / la tuile B entière
    std::vector<float> confidence;              // confiance de chaque paire, entre 0 et 1
};

// Sortie d'un OutlierRejector
struct Alignment {
    cv::Mat transform;            // transformation b -> a (3x3, CV_64F)
    std::vector<uchar> inliers;   // 1 = inlier, même taille que Matches
};

} // namespace stitching
