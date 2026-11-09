// tests/steps/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <chrono>
#include <filesystem>
#include <vector>

// NOLINTBEGIN

namespace {

constexpr int BLOCK_WIDTH = 100;
constexpr int BLOCK_HEIGHT = 100;
constexpr double SAMPLE_COLOR_B = 1000.0;
constexpr double SAMPLE_COLOR_G = 2000.0;
constexpr double SAMPLE_COLOR_R = 3000.0;
constexpr int VALID_MASK_PIXEL = 255;

class TempDirClass {
public:
    TempDirClass() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("kikirpa_test_export_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDirClass() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

} // namespace

TEST(ExportStepTest, RejectsMissingDataStoreOrBlocks) {
    StepsNs::ExportStepClass step;
    EXPECT_FALSE(step.execute());

    StepNs::DataStoreClass store;
    step.setDataStore(store);
    EXPECT_FALSE(step.execute());
}

TEST(ExportStepTest, ExportsValidCompositeBlocksToTiff) {
    TempDirClass tempDir;

    DataNs::CompositeBlock block;
    block.blockRect = cv::Rect(0, 0, BLOCK_WIDTH, BLOCK_HEIGHT);
    block.imageRgb = cv::Mat(BLOCK_HEIGHT, BLOCK_WIDTH, CV_16UC3, cv::Scalar(SAMPLE_COLOR_B, SAMPLE_COLOR_G, SAMPLE_COLOR_R));
    block.provenance = cv::Mat(BLOCK_HEIGHT, BLOCK_WIDTH, CV_16UC1, cv::Scalar(1));
    block.mask = cv::Mat(BLOCK_HEIGHT, BLOCK_WIDTH, CV_8UC1, cv::Scalar(VALID_MASK_PIXEL));

    DataNs::CompositeBlockList blocks = {block};

    StepNs::DataStoreClass store;
    store.add("compositeBlocks", blocks);

    StepsNs::ExportInput input;
    input.outputDirectory = tempDir.path.string();
    StepsNs::ExportStepClass step(input);
    step.setDataStore(store);

    EXPECT_TRUE(step.execute());

    const auto& output = step.getOutput();
    EXPECT_TRUE(std::filesystem::exists(output.imagePath));
    EXPECT_GT(std::filesystem::file_size(output.imagePath), 0u);
    EXPECT_EQ(output.canvasSize.width, BLOCK_WIDTH);
    EXPECT_EQ(output.canvasSize.height, BLOCK_HEIGHT);
    EXPECT_EQ(output.blockCount, 1u);
}

// NOLINTEND
