// tests/seamline/test_territory_builder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

    cv::Mat rasterizeTerritory(const DataNs::TileTerritory& territory,
        const cv::Size& size) {
        cv::Mat mask = cv::Mat::zeros(size, CV_8UC1);
        for (std::size_t polygonIndex = 0;
            polygonIndex < territory.polygons.size(); ++polygonIndex) {
            cv::Mat polygonMask = cv::Mat::zeros(size, CV_8UC1);
            const std::vector<std::vector<cv::Point>> outline{
                territory.polygons[polygonIndex]
            };
            cv::fillPoly(polygonMask, outline, cv::Scalar(255));
            if (!territory.holes[polygonIndex].empty())
                cv::fillPoly(polygonMask, territory.holes[polygonIndex],
                    cv::Scalar(0));
            mask |= polygonMask;
        }
        return mask;
    }

}

TEST(TerritoryBuilderTest, PreservesSeamlinesAndPartitionsTranslatedTiles) {
    DataNs::TileList tiles{
        std::make_shared<DataNs::TileClass>(
            10, std::filesystem::path{}, std::filesystem::path{},
            8, 6, CV_16UC3),
        std::make_shared<DataNs::TileClass>(
            20, std::filesystem::path{}, std::filesystem::path{},
            8, 6, CV_16UC3)
    };
    const std::vector<DataNs::TilePositionData> positions{
        {"r00_c00", {-10.25, -5.75}, 0.0},
        {"r00_c01", {-6.25, -5.75}, 0.0}
    };

    DataNs::Seamline seamline;
    seamline.tileIdA = 10;
    seamline.tileIdB = 20;
    seamline.direction = DataNs::Direction::Right;
    for (int rowIndex = 0; rowIndex < 6; ++rowIndex)
        seamline.points.emplace_back(-5.25, -5.75 + rowIndex);

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

    const cv::Size canvasSize(12, 6);
    const cv::Mat maskA = rasterizeTerritory(territories[0], canvasSize);
    const cv::Mat maskB = rasterizeTerritory(territories[1], canvasSize);

    cv::Mat overlap;
    cv::bitwise_and(maskA, maskB, overlap);
    EXPECT_EQ(cv::countNonZero(overlap), 0);

    cv::Mat coverage;
    cv::bitwise_or(maskA, maskB, coverage);
    EXPECT_EQ(cv::countNonZero(coverage), canvasSize.area());

    EXPECT_EQ(cv::countNonZero(maskA), 6 * 6);
    EXPECT_EQ(cv::countNonZero(maskB), 6 * 6);
}