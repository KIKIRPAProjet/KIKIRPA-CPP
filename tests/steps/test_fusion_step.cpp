// tests/steps/test_fusion_step.cpp
// NOLINTBEGIN
#include "steps/fusion_step.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

constexpr int TILE_WIDTH = 100;
constexpr int TILE_HEIGHT = 100;
constexpr double TILE_SHIFT_X = 80.0;
constexpr int OVERLAP_X = 80;
constexpr int CANVAS_WIDTH = 180;
constexpr int BLOCK_SIZE_512 = 512;
constexpr double COLOR_TILE0_B = 1000.0;
constexpr double COLOR_TILE0_G = 2000.0;
constexpr double COLOR_TILE0_R = 3000.0;
constexpr double COLOR_TILE1_B = 4000.0;
constexpr double COLOR_TILE1_G = 5000.0;
constexpr double COLOR_TILE1_R = 6000.0;

class TempDirClass {
public:
    TempDirClass() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("kikirpa_test_fusion_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDirClass() {
        std::error_code errorCode;
        std::filesystem::remove_all(path, errorCode);
    }
    std::filesystem::path path;
};

void populateSyntheticStore(const std::filesystem::path& dirPath, StepNs::DataStoreClass& store) {
    const std::filesystem::path tilePath0 = dirPath / "t_r00_c00.tif";
    const std::filesystem::path tilePath1 = dirPath / "t_r00_c01.tif";

    const cv::Mat img0(TILE_HEIGHT, TILE_WIDTH, CV_16UC3, cv::Scalar(COLOR_TILE0_B, COLOR_TILE0_G, COLOR_TILE0_R));
    const cv::Mat img1(TILE_HEIGHT, TILE_WIDTH, CV_16UC3, cv::Scalar(COLOR_TILE1_B, COLOR_TILE1_G, COLOR_TILE1_R));
    cv::imwrite(tilePath0.string(), img0);
    cv::imwrite(tilePath1.string(), img1);

    auto tile0 = std::make_shared<DataNs::TileClass>(0, tilePath0, tilePath0, TILE_WIDTH, TILE_HEIGHT, CV_16UC3);
    auto tile1 = std::make_shared<DataNs::TileClass>(1, tilePath1, tilePath1, TILE_WIDTH, TILE_HEIGHT, CV_16UC3);
    const DataNs::TileList tiles = {tile0, tile1};

    const std::vector<DataNs::TilePositionData> positions = {
        {"r00_c00", cv::Point2d(0.0, 0.0), 0.0},
        {"r00_c01", cv::Point2d(TILE_SHIFT_X, 0.0), 0.0}
    };

    DataNs::TileTerritory terr0;
    terr0.tileIndex = 0;
    terr0.polygons = {{{0, 0}, {OVERLAP_X, 0}, {OVERLAP_X, TILE_HEIGHT}, {0, TILE_HEIGHT}}};

    DataNs::TileTerritory terr1;
    terr1.tileIndex = 1;
    terr1.polygons = {{{OVERLAP_X, 0}, {CANVAS_WIDTH, 0}, {CANVAS_WIDTH, TILE_HEIGHT}, {OVERLAP_X, TILE_HEIGHT}}};

    store.add("tiles", tiles);
    store.add("positions", positions);
    store.add("territories", std::vector<DataNs::TileTerritory>{terr0, terr1});
}

} // namespace

TEST(FusionStepTest, RejectsMissingDataStoreOrInputs) {
    StepsNs::FusionStepClass step;
    EXPECT_FALSE(step.execute());

    StepNs::DataStoreClass store;
    step.setDataStore(store);
    EXPECT_FALSE(step.execute());
}

TEST(FusionStepTest, ExecutesOnSyntheticTiles) {
    TempDirClass tempDir;
    StepNs::DataStoreClass store;
    populateSyntheticStore(tempDir.path, store);

    StepsNs::FusionInput input;
    input.blockSize = BLOCK_SIZE_512;
    StepsNs::FusionStepClass step(input);
    step.setDataStore(store);

    EXPECT_TRUE(step.execute());
    ASSERT_TRUE(store.has("compositeBlocks"));

    const auto& blocks = store.get<DataNs::CompositeBlockList>("compositeBlocks");
    ASSERT_FALSE(blocks.empty());
    EXPECT_EQ(blocks[0].blockRect.x, 0);
    EXPECT_EQ(blocks[0].blockRect.y, 0);
    EXPECT_EQ(blocks[0].blockRect.width, CANVAS_WIDTH);
    EXPECT_EQ(blocks[0].blockRect.height, TILE_HEIGHT);
}

// NOLINTEND
