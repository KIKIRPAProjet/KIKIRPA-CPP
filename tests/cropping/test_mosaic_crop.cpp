// tests/cropping/test_mosaic_crop.cpp
#include "algorithms/cropping/mosaic_crop.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>

namespace CroppingTestsNs {

    namespace {

        constexpr int BLOCK_SIZE = 4;
        constexpr int MOSAIC_SIZE = BLOCK_SIZE * 2;
        constexpr int BLOCK_ROW_OR_COLUMN_COUNT = 2;
        constexpr unsigned short RGB_RED_BASE = 1000;
        constexpr unsigned short RGB_GREEN_BASE = 2000;
        constexpr unsigned short RGB_BLUE_BASE = 3000;
        constexpr unsigned short PROVENANCE_BASE = RGB_RED_BASE;
        constexpr unsigned char MASK_ON = 255;
        constexpr int PREVIOUS_RESULT_BLOCK_COUNT = 10;

        const std::array<std::string, 3> FILE_PREFIXES{
            "rgb", "provenance", "mask"
        };

        constexpr std::streamoff PNG_DEPTH_OFFSET = 24;
        constexpr std::streamoff PNG_COLOR_TYPE_OFFSET = 25;

        struct HeaderBytePatch {

            std::streamoff offset;
            unsigned char value;

        };

        std::string getSuffix(const cv::Point& position) {
            return "_x" + std::to_string(position.x) +
                "_y" + std::to_string(position.y) + ".png";
        }

        std::string readFileBytes(const std::filesystem::path& path) {
            std::ifstream stream(path, std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }

        bool applyHeaderBytePatch(
            const std::filesystem::path& path,
            const HeaderBytePatch& patch
        ) {
            std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!stream) {
                return false;
            }

            stream.seekp(patch.offset);
            if (!stream.good()) {
                return false;
            }

            stream.put(static_cast<char>(patch.value));
            return stream.good();
        }

    }

    class MosaicCropTestClass : public testing::Test {

    protected:
        void SetUp() override {
            static std::atomic<unsigned int> sequence{ 0 };
            const auto timestamp =
                std::chrono::steady_clock::now().time_since_epoch().count();

            directory = std::filesystem::temp_directory_path() /
                ("kikirpa_crop_" + std::to_string(timestamp) +
                    "_" + std::to_string(sequence.fetch_add(1)));

            ASSERT_TRUE(std::filesystem::create_directory(directory));
            sourceDirectory = directory / "source";
            ASSERT_TRUE(
                std::filesystem::create_directory(sourceDirectory));

            createImages();
            ASSERT_TRUE(writeSource(cv::Point(0, 0)));
        }

        void TearDown() override {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            EXPECT_FALSE(error) << error.message();
        }

        void createImages() {
            images[0] = cv::Mat(MOSAIC_SIZE, MOSAIC_SIZE, CV_16UC3);
            images[1] = cv::Mat(MOSAIC_SIZE, MOSAIC_SIZE, CV_16UC1);
            images[2] = cv::Mat(MOSAIC_SIZE, MOSAIC_SIZE, CV_8UC1);

            for (int rowIndex = 0; rowIndex < MOSAIC_SIZE; ++rowIndex) {
                for (int columnIndex = 0;
                    columnIndex < MOSAIC_SIZE; ++columnIndex) {
                    const int value =
                        rowIndex * MOSAIC_SIZE + columnIndex;

                    images[0].at<cv::Vec3w>(rowIndex, columnIndex) =
                        cv::Vec3w(
                            static_cast<unsigned short>(RGB_RED_BASE + value),
                            static_cast<unsigned short>(RGB_GREEN_BASE + value),
                            static_cast<unsigned short>(RGB_BLUE_BASE + value));

                    images[1].at<unsigned short>(rowIndex, columnIndex) =
                        static_cast<unsigned short>(PROVENANCE_BASE + value);

                    images[2].at<unsigned char>(rowIndex, columnIndex) =
                        static_cast<unsigned char>(
                            (rowIndex + columnIndex) % 2 == 0 ? MASK_ON : 0);
                }
            }
        }

        bool writeRegionFiles(
            const cv::Rect& region,
            const std::string& suffix
        ) const {
            for (std::size_t fileIndex = 0;
                fileIndex < images.size(); ++fileIndex) {
                const auto path = sourceDirectory /
                    (FILE_PREFIXES[fileIndex] + suffix);

                if (!cv::imwrite(path.string(), images[fileIndex](region))) {
                    return false;
                }
            }

            return true;
        }

        std::array<cv::Rect, BLOCK_ROW_OR_COLUMN_COUNT * BLOCK_ROW_OR_COLUMN_COUNT>
            getSourceRegions(const cv::Point& origin) const {
            std::array<cv::Rect,
                BLOCK_ROW_OR_COLUMN_COUNT * BLOCK_ROW_OR_COLUMN_COUNT> regions{};
            int regionIndex = 0;

            for (int rowIndex = 0; rowIndex < BLOCK_ROW_OR_COLUMN_COUNT; ++rowIndex) {
                for (int columnIndex = 0;
                    columnIndex < BLOCK_ROW_OR_COLUMN_COUNT; ++columnIndex) {
                    regions[regionIndex++] = cv::Rect(
                        origin.x + columnIndex * BLOCK_SIZE,
                        origin.y + rowIndex * BLOCK_SIZE,
                        BLOCK_SIZE,
                        BLOCK_SIZE);
                }
            }

            return regions;
        }

        bool writeSource(const cv::Point& origin) {
            const cv::Point localOrigin(0, 0);
            for (const auto& sourceRegion : getSourceRegions(localOrigin)) {
                const cv::Rect localRegion(
                    sourceRegion.tl(),
                    sourceRegion.size());
                const std::string suffix =
                    getSuffix(sourceRegion.tl() + origin);

                if (!writeRegionFiles(localRegion, suffix)) {
                    return false;
                }
            }

            return true;
        }

        CroppingNs::CropInput makeInput(
            const cv::Rect& rectangle
        ) const {
            CroppingNs::CropInput input;
            input.mosaic = { sourceDirectory.string(), 4 };
            input.rectangle = rectangle;
            input.outputDirectory = directory / "cropped";
            return input;
        }

        void verifyPixels(
            const CroppingNs::CropInput& input,
            const DataNs::MosaicFiles& output,
            const cv::Point& origin = cv::Point(0, 0)
        ) const {
            int expectedBlockCount = 0;

            for (const auto& sourceRegion : getSourceRegions(origin)) {
                const cv::Rect intersection = sourceRegion & input.rectangle;
                if (intersection.empty()) {
                    continue;
                }

                ++expectedBlockCount;
                const cv::Point outputPosition =
                    intersection.tl() - input.rectangle.tl();
                const cv::Rect expectedRegion(
                    intersection.tl() - origin,
                    intersection.size());
                verifyBlock(output, outputPosition, expectedRegion);
            }

            EXPECT_EQ(output.totalBlocks, expectedBlockCount);
        }

        void verifyBlock(
            const DataNs::MosaicFiles& output,
            const cv::Point& position,
            const cv::Rect& expectedRegion
        ) const {
            for (std::size_t fileIndex = 0;
                fileIndex < images.size(); ++fileIndex) {
                const auto path =
                    std::filesystem::path(output.mosaicPath) /
                    (FILE_PREFIXES[fileIndex] + getSuffix(position));

                const cv::Mat actual = cv::imread(
                    path.string(), cv::IMREAD_UNCHANGED);
                const cv::Mat expected =
                    images[fileIndex](expectedRegion);

                ASSERT_FALSE(actual.empty()) << path.string();
                ASSERT_EQ(actual.size(), expected.size());
                ASSERT_EQ(actual.type(), expected.type());
                EXPECT_DOUBLE_EQ(
                    cv::norm(actual, expected, cv::NORM_INF), 0.0);
            }
        }

        std::filesystem::path directory;
        std::filesystem::path sourceDirectory;
        std::array<cv::Mat, 3> images;

    };

    TEST_F(MosaicCropTestClass, PreservesWholeMosaicWithoutReencoding) {
        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        EXPECT_TRUE(errorMessage.empty());
        verifyPixels(input, output);

        for (const auto& entry :
            std::filesystem::directory_iterator(sourceDirectory)) {
            const auto destination =
                std::filesystem::path(output.mosaicPath) /
                entry.path().filename();

            ASSERT_TRUE(std::filesystem::is_regular_file(destination));
            EXPECT_EQ(
                readFileBytes(entry.path()), readFileBytes(destination));
        }
    }

    TEST_F(MosaicCropTestClass, CropsAcrossFourBlocks) {
        const auto input = makeInput(cv::Rect(2, 1, 5, 6));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        EXPECT_EQ(output.totalBlocks, 4);
        verifyPixels(input, output);
    }

    TEST_F(MosaicCropTestClass, CropsInsideOneBlock) {
        const auto input = makeInput(cv::Rect(1, 1, 2, 2));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        EXPECT_EQ(output.totalBlocks, 1);
        verifyPixels(input, output);
    }

    TEST_F(MosaicCropTestClass, KeepsOneWholeBlockAtNewOrigin) {
        const auto input = makeInput(
            cv::Rect(BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        EXPECT_EQ(output.totalBlocks, 1);
        verifyPixels(input, output);

        const auto destination = std::filesystem::path(output.mosaicPath) /
            "rgb_x0_y0.png";
        EXPECT_EQ(
            readFileBytes(sourceDirectory / "rgb_x4_y4.png"),
            readFileBytes(destination));
    }

    TEST_F(MosaicCropTestClass, SupportsNegativeAssemblyCoordinates) {
        std::filesystem::remove_all(sourceDirectory);
        ASSERT_TRUE(std::filesystem::create_directory(sourceDirectory));

        const cv::Point origin(-BLOCK_SIZE, -BLOCK_SIZE);
        ASSERT_TRUE(writeSource(origin));
        const auto input = makeInput(cv::Rect(-3, -2, 5, 5));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        verifyPixels(input, output, origin);
    }

    TEST_F(MosaicCropTestClass, RejectsEmptyRectangle) {
        const auto input = makeInput(cv::Rect(0, 0, 0, BLOCK_SIZE));
        DataNs::MosaicFiles output{
            "previous_result", PREVIOUS_RESULT_BLOCK_COUNT
        };
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsRectangleOutsideMosaic) {
        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE + 1, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsUnexpectedColorDepth) {
        ASSERT_TRUE(applyHeaderBytePatch(
            sourceDirectory / "rgb_x0_y0.png", { PNG_DEPTH_OFFSET, 8 }));

        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsUnexpectedMaskChannelLayout) {
        ASSERT_TRUE(applyHeaderBytePatch(
            sourceDirectory / "mask_x0_y0.png", { PNG_COLOR_TYPE_OFFSET, 2 }));

        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsInconsistentBlockDimensions) {
        const cv::Mat invalidMask(
            BLOCK_SIZE - 1, BLOCK_SIZE, CV_8UC1, cv::Scalar(255));
        ASSERT_TRUE(cv::imwrite(
            (sourceDirectory / "mask_x0_y0.png").string(), invalidMask));

        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsInvalidPngSignature) {
        ASSERT_TRUE(applyHeaderBytePatch(
            sourceDirectory / "rgb_x0_y0.png", { 0, 0 }));

        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsMissingMaskFile) {
        ASSERT_TRUE(std::filesystem::remove(
            sourceDirectory / "mask_x0_y0.png"));
        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, SupportsRectangleAtIntegerCoordinateLimit) {
        std::filesystem::remove_all(sourceDirectory);
        ASSERT_TRUE(std::filesystem::create_directory(sourceDirectory));

        const int originValue =
            std::numeric_limits<int>::max() - MOSAIC_SIZE;
        const cv::Point origin(originValue, originValue);
        ASSERT_TRUE(writeSource(origin));

        const auto input = makeInput(cv::Rect(
            originValue + 1,
            originValue + 2,
            BLOCK_SIZE,
            BLOCK_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(input, output, errorMessage))
            << errorMessage;

        verifyPixels(input, output, origin);
    }

    TEST_F(MosaicCropTestClass, RejectsRectangleOverflowingIntegerLimit) {
        const int maximum = std::numeric_limits<int>::max();
        const auto input = makeInput(
            cv::Rect(maximum - 1, maximum - 1, 2, 1));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsMissingOutputParentDirectory) {
        auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        input.outputDirectory = directory / "missing_parent" / "cropped";

        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, SupportsOutputAliasingInputMosaicOnSuccess) {
        auto input = makeInput(cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        std::string errorMessage;

        ASSERT_TRUE(CroppingNs::cropMosaic(
            input, input.mosaic, errorMessage))
            << errorMessage;

        EXPECT_TRUE(errorMessage.empty());
        EXPECT_EQ(input.mosaic.mosaicPath, input.outputDirectory.string());
        EXPECT_EQ(input.mosaic.totalBlocks, 4);
        verifyPixels(input, input.mosaic);
    }

    TEST_F(MosaicCropTestClass, SupportsOutputAliasingInputMosaicOnFailure) {
        auto input = makeInput(cv::Rect(0, 0, 0, BLOCK_SIZE));
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, input.mosaic, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(input.mosaic.mosaicPath.empty());
        EXPECT_EQ(input.mosaic.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsIncorrectBlockCount) {
        auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        input.mosaic.totalBlocks = 3;
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, PreservesExistingOutputDirectory) {
        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        ASSERT_TRUE(
            std::filesystem::create_directory(input.outputDirectory));

        const auto markerPath = input.outputDirectory / "keep.txt";
        {
            std::ofstream stream(markerPath);
            stream << "existing result";
            ASSERT_TRUE(stream.good());
        }

        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_EQ(readFileBytes(markerPath), "existing result");
        EXPECT_TRUE(output.mosaicPath.empty());
    }

    TEST_F(MosaicCropTestClass, RemovesIncompleteOutputAfterDecodeFailure) {
        const auto colorPath = sourceDirectory / "rgb_x0_y0.png";

        // L'en-tête reste lisible, mais les pixels ne sont plus décodables.
        constexpr std::uintmax_t PNG_HEADER_SIZE = 33;
        std::filesystem::resize_file(colorPath, PNG_HEADER_SIZE);

        const auto input = makeInput(cv::Rect(1, 1, 2, 2));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
        EXPECT_TRUE(std::filesystem::exists(colorPath));
    }

    TEST_F(MosaicCropTestClass, RejectsGapInsideCropRectangle) {
        for (const auto& prefix : FILE_PREFIXES) {
            ASSERT_TRUE(std::filesystem::remove(
                sourceDirectory / (prefix + "_x4_y4.png")));
        }

        auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        input.mosaic.totalBlocks = 3;
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

    TEST_F(MosaicCropTestClass, RejectsOverlapEvenWhenTotalAreaMatches) {
        // Le déplacement crée un chevauchement et un trou de même surface.
        // La somme des surfaces ne suffit donc pas à détecter le problème.
        for (const auto& prefix : FILE_PREFIXES) {
            std::filesystem::rename(
                sourceDirectory / (prefix + "_x4_y0.png"),
                sourceDirectory / (prefix + "_x2_y0.png"));
        }

        const auto input = makeInput(
            cv::Rect(0, 0, MOSAIC_SIZE, MOSAIC_SIZE));
        DataNs::MosaicFiles output;
        std::string errorMessage;

        EXPECT_FALSE(CroppingNs::cropMosaic(input, output, errorMessage));

        EXPECT_FALSE(errorMessage.empty());
        EXPECT_TRUE(output.mosaicPath.empty());
        EXPECT_EQ(output.totalBlocks, 0);
        EXPECT_FALSE(std::filesystem::exists(input.outputDirectory));
    }

}