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

class TempDir {
public:
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("kikirpa_test_fusion_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

} // namespace

TEST(FusionStepTest, RejectsMissingDataStoreOrInputs) {
    StepsNs::FusionStepClass step;
    EXPECT_FALSE(step.execute());

    StepNs::DataStoreClass store;
    step.setDataStore(store);
    EXPECT_FALSE(step.execute());
}

TEST(FusionStepTest, ExecutesOnSyntheticTiles) {
    TempDir tempDir;
    const std::filesystem::path tilePath0 = tempDir.path / "t_r00_c00.tif";
    const std::filesystem::path tilePath1 = tempDir.path / "t_r00_c01.tif";

    const int width = 100;
    const int height = 100;
    cv::Mat img0(height, width, CV_16UC3, cv::Scalar(1000, 2000, 3000));
    cv::Mat img1(height, width, CV_16UC3, cv::Scalar(4000, 5000, 6000));
    ASSERT_TRUE(cv::imwrite(tilePath0.string(), img0));
    ASSERT_TRUE(cv::imwrite(tilePath1.string(), img1));

    auto tile0 = std::make_shared<DataNs::TileClass>(0, tilePath0, tilePath0, width, height, CV_16UC3);
    auto tile1 = std::make_shared<DataNs::TileClass>(1, tilePath1, tilePath1, width, height, CV_16UC3);
    DataNs::TileList tiles = {tile0, tile1};

    std::vector<DataNs::TilePositionData> positions = {
        {"r00_c00", cv::Point2d(0.0, 0.0), 0.0},
        {"r00_c01", cv::Point2d(80.0, 0.0), 0.0}
    };

    // Territoires simples couvrant la mosaïque
    DataNs::TileTerritory terr0;
    terr0.tileIndex = 0;
    terr0.polygons = {{{0, 0}, {80, 0}, {80, 100}, {0, 100}}};

    DataNs::TileTerritory terr1;
    terr1.tileIndex = 1;
    terr1.polygons = {{{80, 0}, {180, 0}, {180, 100}, {80, 100}}};

    std::vector<DataNs::TileTerritory> territories = {terr0, terr1};

    StepNs::DataStoreClass store;
    store.add("tiles", tiles);
    store.add("positions", positions);
    store.add("territories", territories);

    StepsNs::FusionInput input;
    input.blockSize = 512;
    StepsNs::FusionStepClass step(input);
    step.setDataStore(store);

    EXPECT_TRUE(step.execute());
    ASSERT_TRUE(store.has("compositeBlocks"));

    const auto& blocks = store.get<DataNs::CompositeBlockList>("compositeBlocks");
    EXPECT_FALSE(blocks.empty());
    EXPECT_EQ(blocks[0].blockRect.x, 0);
    EXPECT_EQ(blocks[0].blockRect.y, 0);
    EXPECT_EQ(blocks[0].blockRect.width, 180);
    EXPECT_EQ(blocks[0].blockRect.height, 100);
}
