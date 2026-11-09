// tests/core/test_pipeline_2x2.cpp
// NOLINTBEGIN
#include <gtest/gtest.h>

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int OVERLAP_X = 80;
constexpr int OVERLAP_Y = 60;
constexpr int SHIFT_X = TILE_WIDTH - OVERLAP_X;    // 320 px
constexpr int SHIFT_Y = TILE_HEIGHT - OVERLAP_Y;  // 240 px
constexpr int CANVAS_WIDTH = SHIFT_X + TILE_WIDTH;   // 720 px
constexpr int CANVAS_HEIGHT = SHIFT_Y + TILE_HEIGHT; // 540 px
constexpr unsigned int RANDOM_TEXTURE_SEED = 4242;
constexpr double GAUSSIAN_SIGMA = 2.0;
constexpr double NORM_MIN_VALUE = 2000.0;
constexpr double NORM_MAX_VALUE = 60000.0;
constexpr float NOMINAL_OVERLAP = 0.2f;
constexpr float GRAPHCUT_SCALE = 0.5f;
constexpr int BLOCK_SIZE_512 = 512;
constexpr int SIZE_TOLERANCE_PX = 20;
constexpr std::size_t EXPECTED_TILE_COUNT = 4;
constexpr std::size_t EXPECTED_PAIR_COUNT = 4;

struct TempWorkspace {
    TempWorkspace() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("kikirpa_test_2x2_" + std::to_string(now));
        imagesDir = root / "images";
        outputDir = root / "output";
        fs::create_directories(imagesDir);
        fs::create_directories(outputDir);
    }

    ~TempWorkspace() {
        std::error_code errorCode;
        fs::remove_all(root, errorCode);
    }

    fs::path root;
    fs::path imagesDir;
    fs::path outputDir;
};

void createSyntheticTiles2x2(const fs::path& imagesDir) {
    cv::Mat canvas32f(CANVAS_HEIGHT, CANVAS_WIDTH, CV_32FC3);
    cv::RNG rng(RANDOM_TEXTURE_SEED);
    rng.fill(canvas32f, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(canvas32f, canvas32f, cv::Size(), GAUSSIAN_SIGMA);
    cv::normalize(canvas32f, canvas32f, NORM_MIN_VALUE, NORM_MAX_VALUE, cv::NORM_MINMAX);

    cv::Mat canvas16u;
    canvas32f.convertTo(canvas16u, CV_16UC3);

    cv::Mat tileImage00 = canvas16u(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c00.tif").string(), tileImage00);

    cv::Mat tileImage01 = canvas16u(cv::Rect(SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c01.tif").string(), tileImage01);

    cv::Mat tileImage10 = canvas16u(cv::Rect(0, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c00.tif").string(), tileImage10);

    cv::Mat tileImage11 = canvas16u(cv::Rect(SHIFT_X, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c01.tif").string(), tileImage11);
}

Json build2x2PipelineScript(const fs::path& imagesDir, const fs::path& outputDir) {
    return {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", imagesDir.string()}},
                        {{"valueType", "string"}, {"name", "format"}, {"value", ".*_r(?=\\d+_c(\\d+)\\.tiff?$)(\\d+)_c\\d+\\.tiff?"}}
                    })}
                },
                {
                    {"name", "phase-correlation"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "niveau"}, {"value", 0}},
                        {{"valueType", "float"}, {"name", "overlap"}, {"value", NOMINAL_OVERLAP}}
                    })}
                },
                {
                    {"name", "absolute-pos"}
                },
                {
                    {"name", "intensity-correction"}
                },
                {
                    {"name", "graphcut"},
                    {"parameters", Json::array({
                        {{"valueType", "float"}, {"name", "scale"}, {"value", GRAPHCUT_SCALE}}
                    })}
                },
                {
                    {"name", "fusion"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "blockSize"}, {"value", BLOCK_SIZE_512}}
                    })}
                },
                {
                    {"name", "export"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "outputDirectory"}, {"value", outputDir.string()}}
                    })}
                }
            })}
        }}
    };
}

} // namespace

TEST(Pipeline2x2Test, RejectsInvalidConfigurationWithSpecificCause) {
    const Json unknownScript = {
        {"pipeline", {
            {"steps", Json::array({
                {{"name", "step_inexistant"}}
            })}
        }}
    };
    EXPECT_THROW(PipelineNs::buildPipeline(unknownScript), std::invalid_argument);

    const Json invalidChainScript = {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", "dummy/"}}
                    })}
                },
                {{"name", "graphcut"}}
            })}
        }}
    };
    EXPECT_THROW(PipelineNs::buildPipeline(invalidChainScript), std::invalid_argument);
}

TEST(Pipeline2x2Test, ExecutesFullChainOnReal2x2DatasetUntilExport) {
    TempWorkspace workspace;
    createSyntheticTiles2x2(workspace.imagesDir);

    const Json script = build2x2PipelineScript(workspace.imagesDir, workspace.outputDir);
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(script);

    std::vector<std::string> logs;
    const bool success = pipeline.run([&logs](const std::string& message) {
        logs.push_back(message);
        std::cout << "    [pipeline 2x2] " << message << std::endl;
    });

    EXPECT_TRUE(success);

    const auto& store = pipeline.getDataStore();

    ASSERT_TRUE(store.has("tiles"));
    const auto& tiles = store.get<DataNs::TileList>("tiles");
    EXPECT_EQ(tiles.size(), EXPECTED_TILE_COUNT);

    ASSERT_TRUE(store.has("matches"));
    const auto& matches = store.get<std::vector<DataNs::PairMatches>>("matches");
    EXPECT_EQ(matches.size(), EXPECTED_PAIR_COUNT);

    ASSERT_TRUE(store.has("positions"));
    const auto& positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
    EXPECT_EQ(positions.size(), EXPECTED_TILE_COUNT);

    ASSERT_TRUE(store.has("intensityCorrection"));
    const auto& intensityCorrection = store.get<DataNs::IntensityCorrection>("intensityCorrection");
    EXPECT_EQ(intensityCorrection.tileGains.size(), EXPECTED_TILE_COUNT);

    ASSERT_TRUE(store.has("territories"));
    const auto& territories = store.get<std::vector<DataNs::TileTerritory>>("territories");
    EXPECT_EQ(territories.size(), EXPECTED_TILE_COUNT);

    ASSERT_TRUE(store.has("compositeBlocks"));
    const auto& blocks = store.get<DataNs::CompositeBlockList>("compositeBlocks");
    EXPECT_FALSE(blocks.empty());

    const fs::path expectedMosaicPath = workspace.outputDir / "mosaic.tif";
    EXPECT_TRUE(fs::exists(expectedMosaicPath));
    EXPECT_GT(fs::file_size(expectedMosaicPath), 0u);

    cv::Mat exportedImage = cv::imread(expectedMosaicPath.string(), cv::IMREAD_UNCHANGED);
    ASSERT_FALSE(exportedImage.empty());
    EXPECT_EQ(exportedImage.type(), CV_16UC3);
    EXPECT_GE(exportedImage.cols, CANVAS_WIDTH - SIZE_TOLERANCE_PX);
    EXPECT_GE(exportedImage.rows, CANVAS_HEIGHT - SIZE_TOLERANCE_PX);
}

// NOLINTEND
