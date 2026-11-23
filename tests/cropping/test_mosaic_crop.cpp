// tests/cropping/test_mosaic_crop.cpp
#include "cropping/crop_test_support.h"

#include "algorithms/cropping/mosaic_crop.h"
#include "core/data/algorithm_types.h"
#include "core/io/pyramidal_tiff_reader.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <filesystem>
#include <limits>
#include <string>
#include <array>

namespace CroppingTestsNs {

    using MosaicCropTestClass = CropFixtureClass;

    TEST_F(MosaicCropTestClass, PreservesAllChannelsAcrossBlockBoundaries) {
        const cv::Rect rectangle = cropRectangle();
        DataNs::Mosaic output;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic({ source, rectangle, outputDirectory() }, output, error)) << error;
        EXPECT_EQ(output.canvasSize, rectangle.size());
        cv::Mat pixels;
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.imagePath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_16UC3);
        EXPECT_EQ(cv::norm(pixels, color(rectangle), cv::NORM_INF), 0);
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.provenancePath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_16UC1);
        EXPECT_EQ(cv::norm(pixels, provenance(rectangle), cv::NORM_INF), 0);
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.maskPath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_8UC1);
        EXPECT_EQ(cv::norm(pixels, mask(rectangle), cv::NORM_INF), 0);
    }

    TEST_F(MosaicCropTestClass, RejectsRectangleOutsideCanvas) {
        DataNs::Mosaic output;
        std::string error;
        const cv::Rect rectangle(std::numeric_limits<int>::max(), 0, 2, 2);
        EXPECT_FALSE(CroppingNs::cropMosaic({ source, rectangle, outputDirectory() }, output, error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(output.imagePath.empty());
        EXPECT_FALSE(fs::exists(outputDirectory()));
    }

    TEST_F(MosaicCropTestClass, RejectsForeignOutputDirectoryWithoutDeletingIt) {
        ASSERT_TRUE(fs::create_directory(outputDirectory()));
        ASSERT_TRUE(fs::create_directory(outputDirectory() / "keep"));
        DataNs::Mosaic output;
        std::string error;
        EXPECT_FALSE(CroppingNs::cropMosaic({ source, cropRectangle(), outputDirectory() }, output, error));
        EXPECT_TRUE(fs::is_directory(outputDirectory() / "keep"));
    }

    TEST_F(MosaicCropTestClass, RejectsWrongSourceTypeBeforeCreatingOutput) {
        source.maskPath = source.imagePath;
        DataNs::Mosaic output;
        std::string error;
        EXPECT_FALSE(CroppingNs::cropMosaic({ source, cropRectangle(), outputDirectory() }, output, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(fs::exists(outputDirectory()));
    }

    TEST_F(MosaicCropTestClass, ReplacesPreviousCropWithDifferentRectangle) {
        DataNs::Mosaic output;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, output, error)) << error;

        const cv::Rect rectangle(1, 2, 20, 30);
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, rectangle, outputDirectory() }, output, error)) << error;
        EXPECT_EQ(output.canvasSize, rectangle.size());

        cv::Mat pixels;
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.imagePath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_16UC3);
        EXPECT_EQ(cv::norm(pixels, color(rectangle), cv::NORM_INF), 0);

        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.provenancePath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_16UC1);
        EXPECT_EQ(cv::norm(pixels, provenance(rectangle), cv::NORM_INF), 0);

        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.maskPath).readRegion(
            0, { {0, 0}, output.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(pixels.type(), CV_8UC1);
        EXPECT_EQ(cv::norm(pixels, mask(rectangle), cv::NORM_INF), 0);
    }

    TEST_F(MosaicCropTestClass, RecreatesMissingOutputFiles) {
        DataNs::Mosaic output;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, output, error)) << error;

        const std::array<fs::path, 3> paths = {
            output.imagePath, output.provenancePath, output.maskPath
        };

        for (const auto& path : paths) {
            ASSERT_TRUE(fs::remove(path));

            ASSERT_TRUE(CroppingNs::cropMosaic(
                { source, cropRectangle(), outputDirectory() }, output, error)) << error;

            EXPECT_TRUE(fs::is_regular_file(output.imagePath));
            EXPECT_TRUE(fs::is_regular_file(output.provenancePath));
            EXPECT_TRUE(fs::is_regular_file(output.maskPath));

            cv::Mat pixels;
            ASSERT_TRUE(IONs::PyramidalTiffReaderClass(path).readRegion(
                0, { {0, 0}, output.canvasSize }, pixels, error)) << error;

            const cv::Mat expected = path == output.imagePath
                ? color(cropRectangle())
                : (path == output.provenancePath
                    ? provenance(cropRectangle()) : mask(cropRectangle()));

            EXPECT_EQ(pixels.type(), expected.type());
            EXPECT_EQ(cv::norm(pixels, expected, cv::NORM_INF), 0);
        }
    }

    TEST_F(MosaicCropTestClass, InvalidRerunPreservesPreviousResult) {
        DataNs::Mosaic previous;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, previous, error)) << error;

        DataNs::Mosaic invalidSource = source;
        invalidSource.maskPath = source.imagePath;

        DataNs::Mosaic output;
        EXPECT_FALSE(CroppingNs::cropMosaic(
            { invalidSource, cropRectangle(), outputDirectory() }, output, error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(output.imagePath.empty());

        cv::Mat pixels;
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(previous.imagePath).readRegion(
            0, { {0, 0}, previous.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(cv::norm(pixels, color(cropRectangle()), cv::NORM_INF), 0);

        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(previous.provenancePath).readRegion(
            0, { {0, 0}, previous.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(cv::norm(pixels, provenance(cropRectangle()), cv::NORM_INF), 0);

        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(previous.maskPath).readRegion(
            0, { {0, 0}, previous.canvasSize }, pixels, error)) << error;
        EXPECT_EQ(cv::norm(pixels, mask(cropRectangle()), cv::NORM_INF), 0);
    }

    TEST_F(MosaicCropTestClass, RejectsForeignEntriesInOwnedDirectory) {
        DataNs::Mosaic previous;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, previous, error)) << error;

        const fs::path foreignDirectory = outputDirectory() / "keep";
        ASSERT_TRUE(fs::create_directory(foreignDirectory));

        DataNs::Mosaic output;
        EXPECT_FALSE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, output, error));

        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(output.imagePath.empty());
        EXPECT_TRUE(fs::is_directory(foreignDirectory));
        EXPECT_TRUE(fs::is_regular_file(previous.imagePath));
        EXPECT_TRUE(fs::is_regular_file(previous.provenancePath));
        EXPECT_TRUE(fs::is_regular_file(previous.maskPath));
    }

    TEST_F(MosaicCropTestClass, RejectsReplacingSourceDirectory) {
        DataNs::Mosaic previous;
        std::string error;
        ASSERT_TRUE(CroppingNs::cropMosaic(
            { source, cropRectangle(), outputDirectory() }, previous, error)) << error;

        DataNs::Mosaic output;
        EXPECT_FALSE(CroppingNs::cropMosaic(
            { previous, {0, 0, 10, 10}, outputDirectory() }, output, error));

        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(output.imagePath.empty());
        EXPECT_TRUE(fs::is_regular_file(previous.imagePath));
        EXPECT_TRUE(fs::is_regular_file(previous.provenancePath));
        EXPECT_TRUE(fs::is_regular_file(previous.maskPath));
    }

}