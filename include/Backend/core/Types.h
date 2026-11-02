#pragma once

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace stitching {

using Json = nlohmann::json;

namespace keys {
inline constexpr char Tiles[] = "tiles";
inline constexpr char Matches[] = "matches";
inline constexpr char Alignments[] = "alignments";
inline constexpr char Positions[] = "positions";
inline constexpr char Corrections[] = "corrections";
inline constexpr char Territories[] = "territories";
inline constexpr char Mosaic[] = "mosaic";
} // namespace keys

enum class Direction {
    Top,
    Bottom,
    Left,
    Right,
    LeftRight = Right,
    BottomRight = Bottom,
    Below = Bottom
};

struct PairContext {
    Direction dir = Direction::LeftRight;
    float overlap = 0.f;
    std::optional<cv::Point2d> translation;
};

struct RecouvrementReel {
    cv::Point2d translation;
    float overlap = 0.f;
    double rapport = 0.0;
};

struct TileImage {
    std::filesystem::path path;
    int col = 0;
    int row = 0;
    cv::Mat image;
};
using TileImages = std::vector<TileImage>;
using Tiles = std::vector<TileImage>;

struct TilePair {
    std::size_t first = 0;
    std::size_t second = 0;
    Direction direction = Direction::Right;
};

struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;
    std::vector<float> scores;
    float dx = 0.f;
    float dy = 0.f;
};

struct PairMatches {
    TilePair pair;
    std::vector<cv::Point2f> firstPoints;
    std::vector<cv::Point2f> secondPoints;

    std::vector<cv::Point2f> pointsA, pointsB;
    std::vector<float> scores;
    int idA = 0;
    int idB = 0;
    Direction dir = Direction::Right;
    float dx = 0.f;
    float dy = 0.f;
};
using PairMatchesList = std::vector<PairMatches>;

struct Alignment {
    cv::Mat transform;
    std::vector<uchar> inliers;
};

struct PairAlignment {
    TilePair pair;
    cv::Matx33d secondToFirst = cv::Matx33d::eye();
    std::vector<uchar> inliers;
};
using Alignments = std::vector<PairAlignment>;

struct TilePositionData {
    std::string tile_id;
    cv::Point2d position;
    double rotation = 0.0;
    std::size_t tile = 0;
    cv::Matx33d tileToWorld = cv::Matx33d::eye();
};
using TilePosition = TilePositionData;
using Positions = std::vector<TilePositionData>;

struct LightCorrection {
    cv::Scalar gain = cv::Scalar(1.0, 1.0, 1.0, 1.0);
    cv::Scalar bias = cv::Scalar(0.0, 0.0, 0.0, 0.0);
};
using LightCorrections = std::vector<LightCorrection>;

struct Territory {
    cv::Mat mask;
};
using Territories = std::vector<Territory>;

using Mosaic = cv::Mat;

} // namespace stitching
