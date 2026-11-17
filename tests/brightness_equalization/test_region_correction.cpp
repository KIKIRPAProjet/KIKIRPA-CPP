// tests/brightness_equalization/test_region_correction.cpp
#include "algorithms/brightness_equalization/output.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

#include <cstdint>

namespace {

    constexpr int TILE_WIDTH = 37;
    constexpr int TILE_HEIGHT = 23;
    constexpr double GAIN = 1.3;
    constexpr double FIELD_STRENGTH = -0.4;
    constexpr int PATTERN_STEP = 97;
    constexpr int EIGHT_BIT_VALUE = 200;
    constexpr int EIGHT_TO_SIXTEEN_BIT_FACTOR = 257;

    cv::Mat makeTile() {
        cv::Mat tile(TILE_HEIGHT, TILE_WIDTH, CV_16UC3);
        for (int row = 0; row < tile.rows; ++row)
            for (int column = 0; column < tile.cols; ++column)
                tile.at<cv::Vec3w>(row, column) = cv::Vec3w(static_cast<std::uint16_t>(PATTERN_STEP * column),
                                                            static_cast<std::uint16_t>(PATTERN_STEP * row),
                                                            static_cast<std::uint16_t>(PATTERN_STEP * (row + column)));
        return tile;
    }

}

// Le champ d'éclairage est calculé dans le repère de la tuile : corriger quatre régions redonne la tuile corrigée.
TEST(RegionCorrectionTest, CorrectingByRegionsEqualsCorrectingTheWholeTile) {
    const cv::Mat tile = makeTile();
    const cv::Mat wholeTile = BrightnessEqualizationNs::OutputClass::correctTile(tile, GAIN, FIELD_STRENGTH);

    cv::Mat assembled(tile.size(), wholeTile.type());
    const cv::Point split(TILE_WIDTH / 3, TILE_HEIGHT / 2);
    for (const cv::Rect& region : {cv::Rect(0, 0, split.x, split.y), cv::Rect(split.x, 0, TILE_WIDTH - split.x, split.y),
                                   cv::Rect(0, split.y, split.x, TILE_HEIGHT - split.y),
                                   cv::Rect(split, cv::Size(TILE_WIDTH - split.x, TILE_HEIGHT - split.y))}) {
        const BrightnessEqualizationNs::RegionInTile placement{region.tl(), tile.size()};
        BrightnessEqualizationNs::OutputClass::correctRegion(tile(region), placement, GAIN, FIELD_STRENGTH)
            .copyTo(assembled(region));
    }

    EXPECT_EQ(cv::norm(assembled, wholeTile, cv::NORM_INF), 0.0);
}

TEST(RegionCorrectionTest, ScalesEightBitRegionsToSixteenBits) {
    const cv::Mat tile(TILE_HEIGHT, TILE_WIDTH, CV_8UC1, cv::Scalar(EIGHT_BIT_VALUE));
    const BrightnessEqualizationNs::RegionInTile placement{{1, 1}, tile.size()};

    const cv::Mat corrected =
        BrightnessEqualizationNs::OutputClass::correctRegion(tile(cv::Rect(1, 1, 2, 2)), placement, 1.0, 0.0);

    ASSERT_EQ(corrected.type(), CV_16UC1);
    EXPECT_EQ(corrected.at<std::uint16_t>(0, 0), EIGHT_BIT_VALUE * EIGHT_TO_SIXTEEN_BIT_FACTOR);
}
