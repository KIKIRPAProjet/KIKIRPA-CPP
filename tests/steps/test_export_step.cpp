#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <chrono>
#include <filesystem>
#include <vector>

namespace {

class TempDir {
public:
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("kikirpa_test_export_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
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
    TempDir tempDir;

    DataNs::CompositeBlock block;
    block.blockRect = cv::Rect(0, 0, 100, 100);
    block.imageRgb = cv::Mat(100, 100, CV_16UC3, cv::Scalar(1000, 2000, 3000));
    block.provenance = cv::Mat(100, 100, CV_16UC1, cv::Scalar(1));
    block.mask = cv::Mat(100, 100, CV_8UC1, cv::Scalar(255));

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
    EXPECT_EQ(output.canvasSize.width, 100);
    EXPECT_EQ(output.canvasSize.height, 100);
    EXPECT_EQ(output.blockCount, 1u);
}
