// tests/core/test_mosaic_corners.cpp
#include "core/data/mosaic_corners.h"

#include <gtest/gtest.h>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <vector>

namespace {

    int minimumX(const std::vector<cv::Point>& corners) {
        return std::min_element(corners.begin(), corners.end(),
                                [](const cv::Point& first, const cv::Point& second) { return first.x < second.x; })->x;
    }

    int minimumY(const std::vector<cv::Point>& corners) {
        return std::min_element(corners.begin(), corners.end(),
                                [](const cv::Point& first, const cv::Point& second) { return first.y < second.y; })->y;
    }

}

// Cas de l'audit (QW4) : avec un minimum de -12,005, la tuile la plus à gauche doit tomber en 0, pas en 1.
TEST(MosaicCornersTest, PutsTheSmallestFractionalPositionAtZero) {
    const std::vector<cv::Point2d> positions{{-12.005, -18.1}, {7985.4, -17.7}, {-11.7, 5973.2}};

    const std::vector<cv::Point> corners = DataNs::computeMosaicCorners(positions);

    ASSERT_EQ(corners.size(), positions.size());
    EXPECT_EQ(minimumX(corners), 0);
    EXPECT_EQ(minimumY(corners), 0);
    EXPECT_EQ(corners[0], cv::Point(0, 0));
    EXPECT_EQ(corners[1], cv::Point(7997, 0));   // 7985,4 + 12,005 = 7997,405 ; -17,7 + 18,1 = 0,4
    EXPECT_EQ(corners[2], cv::Point(0, 5991));   // -11,7 + 12,005 = 0,305 ; 5973,2 + 18,1 = 5991,3
}

// Une translation entière commune ne change aucun coin.
TEST(MosaicCornersTest, IgnoresACommonIntegerTranslation) {
    const std::vector<cv::Point2d> positions{{0.25, 3.5}, {402.6, -1.2}, {-7.75, 300.4}};
    std::vector<cv::Point2d> shifted;
    for (const cv::Point2d& position : positions) shifted.push_back(position + cv::Point2d(-1000.0, 250.0));

    EXPECT_EQ(DataNs::computeMosaicCorners(shifted), DataNs::computeMosaicCorners(positions));
}

TEST(MosaicCornersTest, ReturnsNoCornerWithoutPosition) {
    EXPECT_TRUE(DataNs::computeMosaicCorners({}).empty());
}
