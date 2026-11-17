// tests/seamline/test_territory_builder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

    constexpr int VALID_PIXEL = 255;
    constexpr int TILE_ID_A = 10;
    constexpr int TILE_ID_B = 20;
    constexpr int TILE_WIDTH = 8;
    constexpr int TILE_HEIGHT = 6;
    constexpr int CANVAS_WIDTH = 12;
    constexpr int TERRITORY_WIDTH = 6;
    constexpr double POSITION_X_A = -10.25;
    constexpr double POSITION_X_B = -6.25;
    constexpr double POSITION_Y = -5.75;
    constexpr double SEAM_X = -5.25;

    cv::Mat rasterizeTerritory(const DataNs::TileTerritory& territory,
        const cv::Size& size) {
        cv::Mat mask = cv::Mat::zeros(size, CV_8UC1);
        for (std::size_t polygonIndex = 0;
            polygonIndex < territory.polygons.size(); ++polygonIndex) {
            cv::Mat polygonMask = cv::Mat::zeros(size, CV_8UC1);
            const std::vector<std::vector<cv::Point>> outline{
                territory.polygons[polygonIndex]
            };
            cv::fillPoly(polygonMask, outline, cv::Scalar(VALID_PIXEL));
            if (!territory.holes[polygonIndex].empty())
                cv::fillPoly(polygonMask, territory.holes[polygonIndex],
                    cv::Scalar(0));
            mask |= polygonMask;
        }
        return mask;
    }

}

// Les positions négatives et fractionnaires vérifient le même changement
// de repère que la fusion. Les identifiants diffèrent volontairement des indices.
TEST(TerritoryBuilderTest, PreservesSeamlinesAndPartitionsTranslatedTiles) {
    DataNs::TileList tiles{
        std::make_shared<DataNs::TileClass>(
            TILE_ID_A, std::filesystem::path{}, std::filesystem::path{},
            TILE_WIDTH, TILE_HEIGHT, CV_16UC3),
        std::make_shared<DataNs::TileClass>(
            TILE_ID_B, std::filesystem::path{}, std::filesystem::path{},
            TILE_WIDTH, TILE_HEIGHT, CV_16UC3)
    };
    const std::vector<DataNs::TilePositionData> positions{
        {"r00_c00", {POSITION_X_A, POSITION_Y}, 0.0},
        {"r00_c01", {POSITION_X_B, POSITION_Y}, 0.0}
    };

    DataNs::Seamline seamline;
    seamline.tileIdA = TILE_ID_A;
    seamline.tileIdB = TILE_ID_B;
    seamline.direction = DataNs::Direction::Right;
    for (int rowIndex = 0; rowIndex < TILE_HEIGHT; ++rowIndex)
        seamline.points.emplace_back(SEAM_X, POSITION_Y + rowIndex);

    const DataNs::Seamlines seamlines{ seamline };
    const auto originalPoints = seamlines.front().points;
    const DynamicProgrammingDijkstraNs::TerritoryInput input{
        tiles, positions, seamlines
    };
    std::vector<DataNs::TileTerritory> territories;
    std::string error;

    ASSERT_TRUE(DynamicProgrammingDijkstraNs::buildTerritories(
        input, territories, error)) << error;

    ASSERT_EQ(territories.size(), 2U);
    EXPECT_EQ(seamlines.front().points, originalPoints);
    EXPECT_EQ(territories[0].tileIndex, 0);
    EXPECT_EQ(territories[1].tileIndex, 1);

    const cv::Size canvasSize(CANVAS_WIDTH, TILE_HEIGHT);
    const cv::Mat maskA = rasterizeTerritory(territories[0], canvasSize);
    const cv::Mat maskB = rasterizeTerritory(territories[1], canvasSize);

    cv::Mat overlap;
    cv::bitwise_and(maskA, maskB, overlap);
    EXPECT_EQ(cv::countNonZero(overlap), 0);

    cv::Mat coverage;
    cv::bitwise_or(maskA, maskB, coverage);
    EXPECT_EQ(cv::countNonZero(coverage), canvasSize.area());

    EXPECT_EQ(cv::countNonZero(maskA), TERRITORY_WIDTH * TILE_HEIGHT);
    EXPECT_EQ(cv::countNonZero(maskB), TERRITORY_WIDTH * TILE_HEIGHT);
}