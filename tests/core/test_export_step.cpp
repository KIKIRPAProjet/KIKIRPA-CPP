// tests/core/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <tiffio.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

    class TemporaryDirectory {
        public:
            TemporaryDirectory() {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("kikirpa-export-" + std::to_string(uniqueValue));
                fs::create_directories(path);
            }

            ~TemporaryDirectory() {
                std::error_code errorCode;
                fs::remove_all(path, errorCode);
            }

            fs::path path;
    };

    DataNs::CompositeBlock makeBlock() {
        DataNs::CompositeBlock block;
        block.blockRect = cv::Rect(0, 0, 2, 2);
        block.imageRgb = cv::Mat(2, 2, CV_16UC3);
        block.imageRgb.at<cv::Vec<std::uint16_t, 3>>(0, 0) = cv::Vec<std::uint16_t, 3>(100, 200, 300);
        block.imageRgb.at<cv::Vec<std::uint16_t, 3>>(0, 1) = cv::Vec<std::uint16_t, 3>(400, 500, 600);
        block.imageRgb.at<cv::Vec<std::uint16_t, 3>>(1, 0) = cv::Vec<std::uint16_t, 3>(700, 800, 900);
        block.imageRgb.at<cv::Vec<std::uint16_t, 3>>(1, 1) = cv::Vec<std::uint16_t, 3>(1000, 1100, 1200);
        return block;
    }

    void configureOutputDirectory(StepsNs::ExportStepClass& step, const fs::path& directory) {
        const Json config = {{"name", "export"},
                             {"parameters", Json::array({Json{{"valueType", "string"},
                                                               {"name", "outputDirectory"},
                                                               {"value", directory.u8string()}}})}};
        step.importConfig(config);
    }

    void readFirstTiffTile(const fs::path& path, cv::Mat& tile, std::uint32_t& width, std::uint32_t& height,
                           std::uint16_t& bitsPerSample, std::uint16_t& sampleCount) {
        TIFF* rawTiff = TIFFOpen(path.string().c_str(), "r");
        ASSERT_NE(rawTiff, nullptr) << path.string();
        const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(rawTiff, &TIFFClose);
        ASSERT_TRUE(TIFFGetField(tiff.get(), TIFFTAG_IMAGEWIDTH, &width));
        ASSERT_TRUE(TIFFGetField(tiff.get(), TIFFTAG_IMAGELENGTH, &height));
        ASSERT_TRUE(TIFFGetField(tiff.get(), TIFFTAG_BITSPERSAMPLE, &bitsPerSample));
        ASSERT_TRUE(TIFFGetField(tiff.get(), TIFFTAG_SAMPLESPERPIXEL, &sampleCount));
        const ttile_t tileIndex = TIFFComputeTile(tiff.get(), 0, 0, 0, 0);
        const tmsize_t tileBytes = TIFFTileSize(tiff.get());
        ASSERT_GT(tileBytes, 0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(tileBytes));
        ASSERT_GT(TIFFReadEncodedTile(tiff.get(), tileIndex, bytes.data(), tileBytes), 0);
        const int type = bitsPerSample == 16 && sampleCount == 3 ? CV_16UC3 :
                         bitsPerSample == 16 ? CV_16UC1 : CV_8UC1;
        tile = cv::Mat(static_cast<int>(DataNs::BLOCK_SIZE), static_cast<int>(DataNs::BLOCK_SIZE), type);
        std::copy(bytes.begin(), bytes.end(), tile.data);
    }

}

TEST(ExportStepTest, WritesOnlyTheCompositeMosaic) {
    TemporaryDirectory temporaryDirectory;
    StepsNs::ExportStepClass step;
    configureOutputDirectory(step, temporaryDirectory.path);
    std::ofstream existingFile(temporaryDirectory.path / "mosaic.tif");
    existingFile << "old output";
    existingFile.close();

    StepNs::DataStoreClass dataStore;
    dataStore.replace("compositeBlocks", DataNs::CompositeBlockList{makeBlock()});
    step.setDataStore(dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const StepsNs::ExportOutput& output = step.getOutput();
    EXPECT_EQ(output.canvasSize, cv::Size(2, 2));
    EXPECT_EQ(output.blockCount, 1u);
    EXPECT_EQ(output.imagePath, temporaryDirectory.path / "mosaic.tif");
    EXPECT_FALSE(output.isBigTiff);

    cv::Mat imageTile;
    std::uint32_t imageWidth = 0, imageHeight = 0;
    std::uint16_t imageBits = 0, imageChannels = 0;
    readFirstTiffTile(output.imagePath, imageTile, imageWidth, imageHeight, imageBits, imageChannels);
    EXPECT_EQ(imageWidth, 2u);
    EXPECT_EQ(imageHeight, 2u);
    EXPECT_EQ(imageBits, 16);
    EXPECT_EQ(imageChannels, 3);
    const cv::Vec<std::uint16_t, 3> pixel = imageTile.at<cv::Vec<std::uint16_t, 3>>(0, 1);
    EXPECT_EQ(pixel[0], 400);
    EXPECT_EQ(pixel[1], 500);
    EXPECT_EQ(pixel[2], 600);

    std::vector<std::string> filesInOutput;
    for (const fs::directory_entry& entry : fs::directory_iterator(temporaryDirectory.path))
        filesInOutput.push_back(entry.path().filename().string());
    EXPECT_EQ(filesInOutput, std::vector<std::string>{"mosaic.tif"});
}

TEST(ExportStepTest, RejectsIncompleteBlockGrid) {
    TemporaryDirectory temporaryDirectory;
    StepsNs::ExportStepClass step;
    configureOutputDirectory(step, temporaryDirectory.path);
    StepNs::DataStoreClass dataStore;
    DataNs::CompositeBlock block = makeBlock();
    block.blockRect.x = DataNs::BLOCK_SIZE;
    dataStore.replace("compositeBlocks", DataNs::CompositeBlockList{block});
    step.setDataStore(dataStore);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult().value("error", "").find("complete output grid"), std::string::npos);
}

TEST(ExportStepTest, ReportsMissingCompositeBlocks) {
    StepsNs::ExportStepClass step;
    StepNs::DataStoreClass dataStore;
    step.setDataStore(dataStore);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult().value("error", "").find("compositeBlocks"), std::string::npos);
}
