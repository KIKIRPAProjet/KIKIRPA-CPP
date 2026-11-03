#include <gtest/gtest.h>

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/import_step.h"
#include "steps/phase_correlation_step.h"
#include "steps/position_step.h"
#include "steps/equalization_step.h"
#include "steps/graph_cut_step.h"
#include "steps/assembly_step.h"
#include "steps/export_step.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

std::filesystem::path makeTestDir(const std::string& tag) {
    const auto root = std::filesystem::temp_directory_path() / ("kikirpa_2x2_" + tag + "_");
    std::filesystem::path dir = root;
    for (int i = 0; i < 10000; ++i) {
        dir = root.string() + std::to_string(i);
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
            return dir;
        }
    }
    throw std::runtime_error("could not create temp dir");
}

void writeTile(const std::filesystem::path& dir, int row, int col, const cv::Mat& img) {
    const std::string name = "img_r" + std::to_string(row) + "_c" + std::to_string(col) + ".tif";
    cv::imwrite((dir / name).string(), img);
}

cv::Mat makeTile(int rows, int cols, int tileId, int row, int col) {
    cv::Mat img(rows, cols, CV_16UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const int r = (x + y + tileId * 17 + row * 7 + col * 13) % 256;
            const int g = (x * 2 + y * 3 + tileId * 19) % 256;
            const int b = ((x + 3 * y + tileId * 11) % 256);
            img.at<cv::Vec<ushort, 3>>(y, x) = cv::Vec<ushort, 3>(r * 257, g * 257, b * 257);
        }
    }
    return img;
}

std::vector<DataNs::TilePositionData> testPositions() {
    return {
        {"r00_c00", {0.0, 0.0}, 0.0},
        {"r00_c01", {512.0, 0.0}, 0.0},
        {"r01_c00", {0.0, 512.0}, 0.0},
        {"r01_c01", {512.0, 512.0}, 0.0},
    };
}

}  // namespace

TEST(FullPipeline2x2, EndToEndSyntheticTiles) {
    const auto dir = makeTestDir("pipeline_2x2");
    const int rows = 512, cols = 512;
    const auto img00 = makeTile(rows, cols, 0, 0, 0);
    const auto img01 = makeTile(rows, cols, 1, 0, 1);
    const auto img10 = makeTile(rows, cols, 2, 1, 0);
    const auto img11 = makeTile(rows, cols, 3, 1, 1);
    writeTile(dir, 0, 0, img00);
    writeTile(dir, 0, 1, img01);
    writeTile(dir, 1, 0, img10);
    writeTile(dir, 1, 1, img11);

    StepNs::DataStoreClass store;
    StepsNs::ImportStepClass importStep({dir.string(), "_r(\\d+)_c(\\d+)\\.tif"});
    importStep.setDataStore(store);
    EXPECT_TRUE(importStep.execute());

    auto tiles = store.get<DataNs::TileList>("tiles");
    ASSERT_EQ(tiles.size(), 4u);

    StepsNs::PhaseCorrelationStepClass phase({0.2f, 0, {}});
    phase.setDataStore(store);
    EXPECT_TRUE(phase.execute());
    ASSERT_TRUE(store.has("matches"));

    StepsNs::PositionStepClass position;
    position.setDataStore(store);
    EXPECT_TRUE(position.execute());
    ASSERT_TRUE(store.has("positions"));

    StepsNs::EqualizationStepClass equalization;
    equalization.setDataStore(store);
    EXPECT_TRUE(equalization.execute());
    ASSERT_TRUE(store.has("corrections"));

    StepsNs::GraphCutStepClass graphcut;
    graphcut.setDataStore(store);
    EXPECT_TRUE(graphcut.execute());
    ASSERT_TRUE(store.has("territories"));

    StepsNs::AssemblyStepClass assemble;
    assemble.setDataStore(store);
    EXPECT_TRUE(assemble.execute());
    ASSERT_TRUE(store.has("mosaic"));

    StepsNs::ExportStepClass exportStep({"output/path.tif"});
    exportStep.setDataStore(store);
    EXPECT_TRUE(exportStep.execute());

    const auto& mosaic = store.get<DataNs::MosaicData>("mosaic");
    EXPECT_FALSE(mosaic.pixels.empty());
    EXPECT_EQ(mosaic.pixels.rows, 1024);
    EXPECT_EQ(mosaic.pixels.cols, 1024);
}

TEST(FullPipeline2x2, PipelineRegistryAcceptsFullChain) {
    auto registry = PipelineNs::stepRegistry();
    EXPECT_TRUE(registry.count("import") > 0);
    EXPECT_TRUE(registry.count("phase-correlation") > 0);
    EXPECT_TRUE(registry.count("position") > 0);
    EXPECT_TRUE(registry.count("equalization") > 0);
    EXPECT_TRUE(registry.count("graphcut") > 0);
    EXPECT_TRUE(registry.count("assemble") > 0);
    EXPECT_TRUE(registry.count("export") > 0);
}
