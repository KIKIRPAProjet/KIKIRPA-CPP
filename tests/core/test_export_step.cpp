// tests/core/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using Json = StepsNs::Json;

namespace {

    constexpr std::array<std::uint16_t, 12> TEST_PIXEL_VALUES{
        100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200,
    };

    struct TiffMetadata {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint16_t bitsPerSample = 0;
        std::uint16_t sampleCount = 0;
    };

    struct TiffTileReadResult {
        TiffMetadata metadata;
        cv::Mat tile;
    };

    class TemporaryDirectoryClass {
        public:
            TemporaryDirectoryClass() {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("kikirpa-export-" + std::to_string(uniqueValue));
                fs::create_directories(path);
            }

            ~TemporaryDirectoryClass() {
                std::error_code errorCode;
                fs::remove_all(path, errorCode);
            }

            fs::path path;
    };

    DataNs::CompositeBlock makeBlock() {
        DataNs::CompositeBlock block;
        block.blockRect = cv::Rect(0, 0, 2, 2);
        block.imageRgb = cv::Mat(2, 2, CV_16UC3);
        std::copy(TEST_PIXEL_VALUES.begin(), TEST_PIXEL_VALUES.end(), block.imageRgb.ptr<std::uint16_t>());
        return block;
    }

    void configureOutputDirectory(StepsNs::ExportStepClass& step, const fs::path& directory) {
        const Json config = {{"name", "export"},
                             {"parameters", Json::array({Json{{"valueType", "string"},
                                                               {"name", "outputDirectory"},
                                                               {"value", directory.u8string()}}})}};
        step.importConfig(config);
    }

    TiffMetadata readTiffMetadata(TIFF* tiff) {
        TiffMetadata metadata;
        if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &metadata.width) ||
            !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &metadata.height) ||
            !TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &metadata.bitsPerSample) ||
            !TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &metadata.sampleCount)) {
            throw std::runtime_error("cannot read TIFF image metadata");
        }
        return metadata;
    }

    cv::Mat readFirstTiffTile(TIFF* tiff, const TiffMetadata& metadata) {
        const ttile_t tileIndex = TIFFComputeTile(tiff, 0, 0, 0, 0);
        const tmsize_t tileBytes = TIFFTileSize(tiff);
        if (tileBytes <= 0) throw std::runtime_error("invalid TIFF tile size");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(tileBytes));
        if (TIFFReadEncodedTile(tiff, tileIndex, bytes.data(), tileBytes) <= 0)
            throw std::runtime_error("cannot read TIFF tile");
        const int type = metadata.bitsPerSample == 16 && metadata.sampleCount == 3 ? CV_16UC3 :
                         metadata.bitsPerSample == 16 ? CV_16UC1 : CV_8UC1;
        cv::Mat tile(static_cast<int>(DataNs::BLOCK_SIZE), static_cast<int>(DataNs::BLOCK_SIZE), type);
        std::copy(bytes.begin(), bytes.end(), tile.data);
        return tile;
    }

    TiffTileReadResult readFirstTiffTile(const fs::path& path) {
        TIFF* rawTiff = TIFFOpen(path.string().c_str(), "r");
        if (!rawTiff) throw std::runtime_error("cannot open TIFF " + path.string());
        const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(rawTiff, &TIFFClose);
        const TiffMetadata metadata = readTiffMetadata(tiff.get());
        return {metadata, readFirstTiffTile(tiff.get(), metadata)};
    }

}

TEST(ExportStepTest, WritesOnlyTheCompositeMosaic) {
    TemporaryDirectoryClass temporaryDirectory;
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

    const TiffTileReadResult tiff = readFirstTiffTile(output.imagePath);
    EXPECT_EQ(tiff.metadata.width, 2u);
    EXPECT_EQ(tiff.metadata.height, 2u);
    EXPECT_EQ(tiff.metadata.bitsPerSample, 16);
    EXPECT_EQ(tiff.metadata.sampleCount, 3);
    const cv::Vec<std::uint16_t, 3> pixel = tiff.tile.at<cv::Vec<std::uint16_t, 3>>(0, 1);
    EXPECT_EQ(pixel[0], TEST_PIXEL_VALUES[3]);
    EXPECT_EQ(pixel[1], TEST_PIXEL_VALUES[4]);
    EXPECT_EQ(pixel[2], TEST_PIXEL_VALUES[5]);

    std::vector<std::string> filesInOutput;
    for (const fs::directory_entry& entry : fs::directory_iterator(temporaryDirectory.path))
        filesInOutput.push_back(entry.path().filename().string());
    EXPECT_EQ(filesInOutput, std::vector<std::string>{"mosaic.tif"});
}

TEST(ExportStepTest, RejectsIncompleteBlockGrid) {
    TemporaryDirectoryClass temporaryDirectory;
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
