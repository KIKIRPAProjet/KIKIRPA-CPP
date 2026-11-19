// tests/cropping/crop_test_support.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace CroppingTestsNs {

    namespace fs = std::filesystem;
    constexpr int SOURCE_WIDTH = DataNs::BLOCK_SIZE + 18;
    constexpr int SOURCE_HEIGHT = DataNs::BLOCK_SIZE + 7;
    constexpr int CROP_LEFT = 7;
    constexpr int CROP_TOP = 5;
    constexpr int CROP_WIDTH = DataNs::BLOCK_SIZE + 7;
    constexpr int CROP_HEIGHT = DataNs::BLOCK_SIZE + 1;
    constexpr int VALID_PIXEL = 255;
    constexpr int PROVENANCE_OFFSET = 42;
    constexpr int COLOR_OFFSET = 1000;

    class CropFixtureClass : public testing::Test {

    protected:
        void SetUp() override {
            static std::atomic<unsigned int> sequence{ 0 };
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory = fs::temp_directory_path() /
                ("mosaic_crop_" + std::to_string(stamp) + "_" + std::to_string(sequence.fetch_add(1)));
            ASSERT_TRUE(fs::create_directory(directory));
            source = { directory / "source_image.tif", directory / "source_provenance.tif",
                      directory / "source_mask.tif", {SOURCE_WIDTH, SOURCE_HEIGHT} };
            buildPixels();
            ASSERT_NO_FATAL_FAILURE(writeImage(source.imagePath, color));
            ASSERT_NO_FATAL_FAILURE(writeImage(source.provenancePath, provenance));
            ASSERT_NO_FATAL_FAILURE(writeImage(source.maskPath, mask));
        }

        void TearDown() override {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }

        void buildPixels() {
            color.create(SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC3);
            provenance.create(SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC1);
            mask.create(SOURCE_HEIGHT, SOURCE_WIDTH, CV_8UC1);
            for (int row = 0; row < SOURCE_HEIGHT; ++row) {
                for (int column = 0; column < SOURCE_WIDTH; ++column) {
                    color.at<cv::Vec3w>(row, column) = {
                        static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(COLOR_OFFSET + row + column) };
                    provenance.at<std::uint16_t>(row, column) =
                        static_cast<std::uint16_t>(PROVENANCE_OFFSET + column);
                    mask.at<unsigned char>(row, column) =
                        row % 2 == 0 ? static_cast<unsigned char>(VALID_PIXEL) : 0;
                }
            }
        }

        void writeImage(const fs::path& path, const cv::Mat& pixels) {
            IONs::TiledTiffWriterClass writer;
            std::string error;
            ASSERT_TRUE(writer.open(path, pixels.size(), pixels.type(), error)) << error;
            for (int top = 0; top < pixels.rows; top += DataNs::BLOCK_SIZE) {
                for (int left = 0; left < pixels.cols; left += DataNs::BLOCK_SIZE) {
                    const cv::Rect area(left, top, std::min(DataNs::BLOCK_SIZE, pixels.cols - left),
                        std::min(DataNs::BLOCK_SIZE, pixels.rows - top));
                    ASSERT_TRUE(writer.writeBlock(area.tl(), pixels(area), error)) << error;
                }
            }
            ASSERT_TRUE(writer.close(error)) << error;
        }

        fs::path outputDirectory() const {
            return directory / IONs::pathFromUtf8(u8"résultat_作品");
        }

        cv::Rect cropRectangle() const {
            return { CROP_LEFT, CROP_TOP, CROP_WIDTH, CROP_HEIGHT };
        }

        fs::path directory;
        DataNs::Mosaic source;
        cv::Mat color;
        cv::Mat provenance;
        cv::Mat mask;

    };

}