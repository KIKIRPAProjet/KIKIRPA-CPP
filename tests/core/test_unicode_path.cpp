// tests/core/test_unicode_path.cpp
// Chemins non ASCII : « œuvre_été » (présent dans la page de code Windows-1252) et « 作品 » (absent de toute page de
// code européenne), pour couvrir les deux voies de IONs::readImage / writeImage.
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tiffio.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

    constexpr int IMAGE_WIDTH = 64;
    constexpr int IMAGE_HEIGHT = 48;
    constexpr double PIXEL_VALUE = 40000.0;

    // Les deux noms de dossier, en UTF-8 (sources compilées avec /utf-8 sous MSVC). Le paramètre des tests est leur
    // index : un paramètre texte non ASCII s'affiche sur deux lignes dans --gtest_list_tests, ce qui casse
    // gtest_discover_tests.
    const char* const LATIN_NAME = "œuvre_été";
    const char* const CJK_NAME = "作品";
    const char* const FOLDER_NAMES[] = {LATIN_NAME, CJK_NAME};
    const char* const FOLDER_LABELS[] = {"Latin", "Cjk"};

    class UnicodePathTestClass : public ::testing::TestWithParam<int> {

        protected:
            void SetUp() override {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                root = fs::temp_directory_path() / ("kikirpa-unicode-test-" + std::to_string(uniqueValue));
                directory = root / IONs::pathFromUtf8(folderName());
                fs::create_directories(directory);
            }

            void TearDown() override {
                std::error_code errorCode;
                fs::remove_all(root, errorCode);
            }

            const char* folderName() const { return FOLDER_NAMES[GetParam()]; }

            fs::path root;
            fs::path directory;

    };

    cv::Mat makeImage() { return {IMAGE_HEIGHT, IMAGE_WIDTH, CV_16UC3, cv::Scalar::all(PIXEL_VALUE)}; }

}

TEST(UnicodePathTest, ConvertsUtf8BothWays) {
    const std::string utf8Path = std::string("dossier/") + CJK_NAME + "/" + LATIN_NAME + ".tif";

    const fs::path path = IONs::pathFromUtf8(utf8Path);

    EXPECT_EQ(path.filename(), IONs::pathFromUtf8(std::string(LATIN_NAME) + ".tif"));
    EXPECT_EQ(IONs::pathToUtf8(path), utf8Path);
}

TEST_P(UnicodePathTestClass, WritesAndReadsAnImage) {
    const fs::path imagePath = directory / IONs::pathFromUtf8(std::string(folderName()) + ".tif");

    ASSERT_TRUE(IONs::writeImage(imagePath, makeImage()));
    ASSERT_TRUE(fs::exists(imagePath));
    const cv::Mat image = IONs::readImage(imagePath, cv::IMREAD_UNCHANGED);

    ASSERT_EQ(image.size(), cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT));
    EXPECT_EQ(image.type(), CV_16UC3);
    EXPECT_EQ(cv::norm(image, makeImage(), cv::NORM_INF), 0.0);
}

TEST_P(UnicodePathTestClass, MissingImageGivesAnEmptyMatrix) {
    EXPECT_TRUE(IONs::readImage(directory / "absent.tif", cv::IMREAD_UNCHANGED).empty());
}

TEST_P(UnicodePathTestClass, OpensATiff) {
    const fs::path tiffPath = directory / "image.tif";
    ASSERT_TRUE(IONs::writeImage(tiffPath, makeImage()));

    const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(IONs::openTiff(tiffPath, "r"), &TIFFClose);

    EXPECT_NE(tiffFile, nullptr);
}

// Le cache d'import (écrit par libtiff) se relit par région dans un dossier non ASCII.
TEST_P(UnicodePathTestClass, WritesAndReadsAPyramidalTiff) {
    const fs::path tiffPath = directory / "pyramide.tif";
    std::string errorMessage;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(makeImage(), tiffPath, errorMessage)) << errorMessage;

    cv::Mat region;
    ASSERT_TRUE(IONs::PyramidalTiffReaderClass(tiffPath).readRegion(0, cv::Rect(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT), region,
                                                                   errorMessage))
        << errorMessage;
    EXPECT_EQ(region.size(), cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT));
}

INSTANTIATE_TEST_SUITE_P(Folders, UnicodePathTestClass, ::testing::Values(0, 1),
                         [](const ::testing::TestParamInfo<int>& info) { return std::string(FOLDER_LABELS[info.param]); });
