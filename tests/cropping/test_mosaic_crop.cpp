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

    TEST_F(MosaicCropTestClass, RejectsExistingOutputWithoutDeletingIt) {
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

}