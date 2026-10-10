#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <filesystem>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

enum class Direction { BottomRight, LeftRight };

// Contexte d'une paire de tuiles voisines, transmis aux algorithmes
struct PairContext {
    Direction dir = Direction::LeftRight;
    float overlap = 0.f;   // recouvrement estimé (fraction de la tuile)
};

// PipelineOption "Tiles" : les images chargées par le step import
struct TileImage {
    std::filesystem::path path;
    int col = 0, row = 0;   // position dans la grille, lue dans le nom du fichier
    cv::Mat image;
};
using Tiles = std::vector<TileImage>;

// Sortie d'un FeatureMatcher : pointsA[i] <-> pointsB[i]
struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;
};

// Sortie d'un OutlierRejector
struct Alignment {
    cv::Mat transform;            // transformation b -> a (3x3, CV_64F)
    std::vector<uchar> inliers;   // 1 = inlier, même taille que Matches
};

} // namespace stitching
