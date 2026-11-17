// NOLINTBEGIN
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>   // NOLINT(misc-include-cleaner)
#endif

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int EXIT_INVALID_USAGE = 2;
constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int OVERLAP_X = 80;
constexpr int OVERLAP_Y = 60;
constexpr int SHIFT_X = TILE_WIDTH - OVERLAP_X;
constexpr int SHIFT_Y = TILE_HEIGHT - OVERLAP_Y;
constexpr int CANVAS_WIDTH = SHIFT_X + TILE_WIDTH;
constexpr int CANVAS_HEIGHT = SHIFT_Y + TILE_HEIGHT;
constexpr int SIZE_TOLERANCE_PX = 20;

struct Workspace {
    Workspace() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("kikirpa_cli_2x2_" + std::to_string(now));
        fs::create_directories(root / "images");
    }

    ~Workspace() {
        std::error_code errorCode;
        fs::remove_all(root, errorCode);
    }

    fs::path root;
};

struct CommandResult {
    int exitCode = -1;
    std::string output;
    std::string errors;
};

std::string quote(const fs::path& path) {
    return "\"" + path.u8string() + "\"";
}

std::string readText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int decodeExitCode(int systemStatus) {
#ifdef _WIN32
    return systemStatus;
#else
    return systemStatus != -1 && WIFEXITED(systemStatus) ? WEXITSTATUS(systemStatus) : -1;
#endif
}

CommandResult runStitching(const std::string& arguments, const fs::path& logDirectory) {
    const fs::path executable = fs::u8path(STITCHING_EXECUTABLE);
    const fs::path outputPath = logDirectory / "stdout.txt";
    const fs::path errorPath = logDirectory / "stderr.txt";
    const std::string command = quote(executable) + " " + arguments + " > " + quote(outputPath) + " 2> " + quote(errorPath);

    int systemStatus = -1;
#ifdef _WIN32
    const std::wstring wideCommand = fs::u8path(command).wstring();
    // NOLINTNEXTLINE(bugprone-command-processor) : launch the built executable as a CLI integration test.
    systemStatus = _wsystem(wideCommand.c_str());
#else
    // NOLINTNEXTLINE(bugprone-command-processor) : launch the built executable as a CLI integration test.
    systemStatus = std::system(command.c_str());
#endif

    return {decodeExitCode(systemStatus), readText(outputPath), readText(errorPath)};
}

void createSyntheticTiles(const fs::path& imageDirectory) {
    cv::Mat texture(CANVAS_HEIGHT, CANVAS_WIDTH, CV_32FC1);
    cv::RNG randomGenerator(4242);
    randomGenerator.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(texture, texture, cv::Size(), 2.0);
    cv::normalize(texture, texture, 5000.0, 35000.0, cv::NORM_MINMAX);

    std::vector<cv::Mat> channels(3);
    texture.convertTo(channels[0], CV_16U);
    texture.convertTo(channels[1], CV_16U, 0.7, 10000.0);
    texture.convertTo(channels[2], CV_16U, 0.4, 20000.0);
    cv::Mat canvas16;
    cv::merge(channels, canvas16);
    const cv::Rect tileRegions[] = {
        {0, 0, TILE_WIDTH, TILE_HEIGHT},
        {SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT},
        {0, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT},
        {SHIFT_X, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT}
    };
    const char* tileNames[] = {
        "img_c00_r00_c00.tif",
        "img_c01_r00_c01.tif",
        "img_c00_r01_c00.tif",
        "img_c01_r01_c01.tif"
    };
    for (std::size_t tileIndex = 0; tileIndex < 4; ++tileIndex)
        if (!cv::imwrite((imageDirectory / tileNames[tileIndex]).string(), canvas16(tileRegions[tileIndex])))
            throw std::runtime_error("could not write a synthetic tile");
}

Json makeScript() {
    const Json steps = Json::array({
        {{"name", "import"},
         {"parameters", Json::array({
             {{"valueType", "string"}, {"name", "path"}, {"value", "images"}},
             {{"valueType", "string"}, {"name", "format"}, {"value", ".*_c(\\d+)_r(\\d+)_c\\d+\\.tiff?"}}
         })}},
        {{"name", "phase-correlation"},
         {"parameters", Json::array({
             {{"valueType", "int"}, {"name", "niveau"}, {"value", 0}},
             {{"valueType", "float"}, {"name", "overlap"}, {"value", 0.2}}
         })}},
        {{"name", "absolute-pos"}},
        {{"name", "intensity-correction"},
         {"parameters", Json::array({{{"valueType", "int"}, {"name", "borderMargin"}, {"value", 5}}})}},
        {{"name", "graphcut"},
         {"parameters", Json::array({{{"valueType", "float"}, {"name", "scale"}, {"value", 0.5}}})}},
        {{"name", "fusion"},
         {"parameters", Json::array({{{"valueType", "int"}, {"name", "blockSize"}, {"value", 512}}})}},
        {{"name", "export"},
         {"parameters", Json::array({{{"valueType", "string"}, {"name", "outputDirectory"}, {"value", "output"}}})}}
    });
    return {{"pipeline", {{"steps", steps}}}, {"run", {{"checkpointDirectory", "checkpoints"}}}};
}

void writeScript(const fs::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << makeScript().dump(2) << '\n';
    if (!output) throw std::runtime_error("could not write the CLI test script");
}

}

TEST(CommandLineTest, HelpDoesNotOpenTheFrontend) {
    Workspace workspace;
    const CommandResult result = runStitching("--help", workspace.root);

    EXPECT_EQ(result.exitCode, EXIT_SUCCESS);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

TEST(CommandLineTest, UnknownOptionIsRejected) {
    Workspace workspace;
    const CommandResult result = runStitching("--pipline script.json", workspace.root);

    EXPECT_EQ(result.exitCode, EXIT_INVALID_USAGE);
    EXPECT_NE(result.errors.find("unknown option '--pipline'"), std::string::npos);
}

TEST(Pipeline2x2CliTest, RunsAndResumesFullPipelineFromScriptDirectory) {
    Workspace workspace;
    createSyntheticTiles(workspace.root / "images");
    const fs::path scriptPath = workspace.root / "pipeline.json";
    writeScript(scriptPath);
    const std::string command = "--pipeline " + quote(scriptPath);

    const CommandResult firstRun = runStitching(command, workspace.root);
    ASSERT_EQ(firstRun.exitCode, EXIT_SUCCESS) << firstRun.output << firstRun.errors;
    EXPECT_NE(firstRun.output.find("Step 7/7 OK"), std::string::npos) << firstRun.output;

    const fs::path mosaicPath = workspace.root / "output" / "mosaic.tif";
    ASSERT_TRUE(fs::exists(mosaicPath));
    EXPECT_GT(fs::file_size(mosaicPath), 0U);
    const cv::Mat mosaic = cv::imread(mosaicPath.string(), cv::IMREAD_UNCHANGED);
    ASSERT_FALSE(mosaic.empty());
    EXPECT_EQ(mosaic.type(), CV_16UC3);
    EXPECT_GE(mosaic.cols, CANVAS_WIDTH - SIZE_TOLERANCE_PX);
    EXPECT_GE(mosaic.rows, CANVAS_HEIGHT - SIZE_TOLERANCE_PX);
    const cv::Mat sourceTile = cv::imread((workspace.root / "images" / "img_c00_r00_c00.tif").string(),
                                          cv::IMREAD_UNCHANGED);
    ASSERT_FALSE(sourceTile.empty());
    const cv::Rect colorCheckRegion(50, 50, 200, 150);
    const cv::Scalar sourceMean = cv::mean(sourceTile(colorCheckRegion));
    const cv::Scalar mosaicMean = cv::mean(mosaic(colorCheckRegion));
    ASSERT_GT(mosaicMean[2], 0.0);
    EXPECT_LT(sourceMean[0], sourceMean[2]);
    EXPECT_NEAR(mosaicMean[0] / mosaicMean[2], sourceMean[0] / sourceMean[2], 0.03);

    std::ifstream runStateInput(workspace.root / "checkpoints" / "run.json", std::ios::binary);
    ASSERT_TRUE(runStateInput.good());
    const Json runState = Json::parse(runStateInput);
    EXPECT_EQ(runState.at("run").at("status"), "completed");
    EXPECT_EQ(runState.at("run").at("completedSteps").size(), 7U);

    const CommandResult resumedRun = runStitching(command + " --resume", workspace.root);
    EXPECT_EQ(resumedRun.exitCode, EXIT_SUCCESS) << resumedRun.output << resumedRun.errors;
    EXPECT_NE(resumedRun.output.find("resuming at step"), std::string::npos) << resumedRun.output;
    EXPECT_NE(resumedRun.output.find("Step 7/7 OK"), std::string::npos) << resumedRun.output;
}

// NOLINTEND
