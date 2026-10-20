#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

// Position de la tuile B par rapport à la tuile A.
// Top/Bottom/Left/Right : corrélation de phase. LeftRight et BottomRight (anciens noms, SIFT et placement
// global) valent respectivement Right (B à droite de A) et Bottom (B sous A).
enum class Direction { Top, Bottom, Left, Right, LeftRight = Right, BottomRight = Bottom };

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
//   dx, dy : translation estimée (coin haut-gauche de B dans le repère de A, en px : pointsA - pointsB)
//   scores : score de confiance de chaque paire de points (entre 0 et 1)
struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;
    std::vector<float> scores;
    float dx = 0.f;
    float dy = 0.f;
};

// Sortie d'un OutlierRejector
struct Alignment {
    cv::Mat transform;            // transformation b -> a (3x3, CV_64F)
    std::vector<uchar> inliers;   // 1 = inlier, même taille que Matches
};

// Position d'une tuile calculée par le placement global (TilePositioner)
struct TilePositionData {
    std::string tile_id;
    cv::Point2d position;
};

} // namespace stitching
