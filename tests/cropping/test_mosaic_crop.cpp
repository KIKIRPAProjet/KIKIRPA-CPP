// tests/cropping/test_mosaic_crop.cpp
#include "algorithms/cropping/mosaic_crop.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace CroppingTestsNs {

    namespace {

        constexpr int BLOCK_SIZE = 4;
        constexpr int MOSAIC_SIZE = BLOCK_SIZE * 2;

        const std::array<std::string, 3> FILE_PREFIXES{
            "rgb", "provenance", "mask"
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
                            static_cast<unsigned short>(1000 + value),
                            static_cast<unsigned short>(2000 + value),
                            static_cast<unsigned short>(3000 + value));

                    images[1].at<unsigned short>(rowIndex, columnIndex) =
                        static_cast<unsigned short>(1000 + value);

                    images[2].at<unsigned char>(rowIndex, columnIndex) =
                        static_cast<unsigned char>(
                            (rowIndex + columnIndex) % 2 == 0 ? 255 : 0);
                }
            }
        }

        bool writeSource(const cv::Point& origin) {
            for (int rowIndex = 0; rowIndex < 2; ++rowIndex) {
                for (int columnIndex = 0;
                    columnIndex < 2; ++columnIndex) {
                    const cv::Rect region(
                        columnIndex * BLOCK_SIZE,
                        rowIndex * BLOCK_SIZE,
                        BLOCK_SIZE,
                        BLOCK_SIZE);

                    const std::string suffix =
                        getSuffix(region.tl() + origin);

                    for (std::size_t fileIndex = 0;
                        fileIndex < images.size(); ++fileIndex) {
                        const auto path = sourceDirectory /
                            (FILE_PREFIXES[fileIndex] + suffix);

                        if (!cv::imwrite(
                            path.string(), images[fileIndex](region))) {
                            return false;
                        }
                    }
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

            for (int rowIndex = 0; rowIndex < 2; ++rowIndex) {
                for (int columnIndex = 0;
                    columnIndex < 2; ++columnIndex) {
                    const cv::Rect sourceRegion(
                        origin.x + columnIndex * BLOCK_SIZE,
                        origin.y + rowIndex * BLOCK_SIZE,
                        BLOCK_SIZE,
                        BLOCK_SIZE);

                    const cv::Rect intersection =
                        sourceRegion & input.rectangle;
                    if (intersection.empty()) {
                        continue;
                    }

                    ++expectedBlockCount;
                    const cv::Point outputPosition =
                        intersection.tl() - input.rectangle.tl();
                    const cv::Rect expectedRegion(
                        intersection.tl() - origin,
                        intersection.size());

                    verifyBlock(
                        output, outputPosition, expectedRegion);
                }
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
        DataNs::MosaicFiles output{ "previous_result", 10 };
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