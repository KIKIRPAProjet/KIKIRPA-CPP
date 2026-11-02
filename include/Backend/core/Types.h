#pragma once

#include <opencv2/core.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace stitching {

namespace keys {
inline constexpr char Tiles[] = "tiles";
inline constexpr char Matches[] = "matches";
inline constexpr char Alignments[] = "alignments";
inline constexpr char Positions[] = "positions";
inline constexpr char Corrections[] = "corrections";
inline constexpr char Territories[] = "territories";
inline constexpr char Mosaic[] = "mosaic";
} // namespace keys

enum class Direction { Right, Below };

struct TileImage {
    std::filesystem::path path;
    int col = 0;
    int row = 0;   // position dans la grille, lue dans le nom du fichier
    cv::Mat image;
};
using Tiles = std::vector<TileImage>;

struct TilePair {
    std::size_t first = 0;
    std::size_t second = 0;
    Direction direction = Direction::Right;
};

struct PairMatches {
    TilePair pair;
    std::vector<cv::Point2f> firstPoints;
    std::vector<cv::Point2f> secondPoints;
};

using Matches = std::vector<PairMatches>;

struct PairAlignment {
    TilePair pair;
    cv::Matx33d secondToFirst = cv::Matx33d::eye();
    std::vector<uchar> inliers;
};
using Alignments = std::vector<PairAlignment>;

struct TilePosition {
    std::size_t tile = 0;
    cv::Matx33d tileToWorld = cv::Matx33d::eye();
};
using Positions = std::vector<TilePosition>;

struct LightCorrection {
    cv::Scalar gain = cv::Scalar(1.0, 1.0, 1.0, 1.0);
    cv::Scalar bias = cv::Scalar(0.0, 0.0, 0.0, 0.0);
};
using LightCorrections = std::vector<LightCorrection>;
