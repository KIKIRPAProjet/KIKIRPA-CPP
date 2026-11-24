// tests/frontend/test_image_layer_loader.cpp
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"
#include "image_layer_loader.h"

#include <QColor>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <opencv2/core/mat.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    constexpr int FIRST_TILE_ID = 7;
    constexpr int SECOND_TILE_ID = 42;
    constexpr int IMAGE_WIDTH = 32;
    constexpr int IMAGE_HEIGHT = 24;

    class ImageLayerLoaderTestClass : public testing::Test {

        protected:
            void SetUp() override {
                ASSERT_TRUE(directory.isValid());
                const cv::Mat image(IMAGE_HEIGHT, IMAGE_WIDTH, CV_16UC3,
                                    cv::Scalar(16 * 256, 32 * 256, 192 * 256));
                const auto firstPath = IONs::pathFromUtf8(
                    directory.filePath(QStringLiteral("detail of the painting.tiff")).toStdString());
                const auto secondPath = IONs::pathFromUtf8(
                    directory.filePath(QStringLiteral("another view.tif")).toStdString());
                std::string errorMessage;
                ASSERT_TRUE(IONs::createPyramidalTiledTiff(image, firstPath, errorMessage)) << errorMessage;
                ASSERT_TRUE(IONs::createPyramidalTiledTiff(image, secondPath, errorMessage)) << errorMessage;

                tiles = {
                    std::make_shared<DataNs::TileClass>(FIRST_TILE_ID, firstPath, firstPath,
                                                        image.cols, image.rows, image.type()),
                    std::make_shared<DataNs::TileClass>(SECOND_TILE_ID, secondPath, secondPath,
                                                        image.cols, image.rows, image.type())
                };
            }

            QTemporaryDir directory;
            DataNs::TileList tiles;
            std::vector<DataNs::TilePositionData> positions = {
                {FIRST_TILE_ID, {12.0, -4.0}, 0.0},
                {SECOND_TILE_ID, {40.0, 16.0}, 0.0}
            };
    };

    TEST_F(ImageLayerLoaderTestClass, MatchesShuffledPositionsByTileId) {
        const std::vector<DataNs::TilePositionData> shuffledPositions = {positions[1], positions[0]};
        const auto layers = buildImageLayers(tiles, shuffledPositions, 1);

        ASSERT_EQ(layers.size(), 2);
        EXPECT_EQ(layers[0].id, QString::number(FIRST_TILE_ID));
        EXPECT_EQ(layers[0].worldRect, QRectF(6, -2, IMAGE_WIDTH / 2.0, IMAGE_HEIGHT / 2.0));
        EXPECT_EQ(layers[1].id, QString::number(SECOND_TILE_ID));
        EXPECT_EQ(layers[1].worldRect, QRectF(20, 8, IMAGE_WIDTH / 2.0, IMAGE_HEIGHT / 2.0));
    }

    TEST_F(ImageLayerLoaderTestClass, ConvertsColorTiffToQImage) {
        const auto layer = loadLowResolutionLayer(*tiles.front(), positions.front(), 0);

        ASSERT_FALSE(layer.image.isNull());
        EXPECT_EQ(layer.image.size(), QSize(IMAGE_WIDTH, IMAGE_HEIGHT));
        EXPECT_EQ(layer.image.pixelColor(0, 0), QColor(192, 32, 16));
        EXPECT_EQ(layer.image.pixelColor(IMAGE_WIDTH - 1, IMAGE_HEIGHT - 1), QColor(192, 32, 16));
    }

    TEST_F(ImageLayerLoaderTestClass, AcceptsArbitraryTiffNames) {
        const auto layers = buildImageLayers(tiles, positions);

        ASSERT_EQ(layers.size(), 2);
        EXPECT_EQ(layers[0].name, QStringLiteral("detail of the painting.tiff"));
        EXPECT_EQ(layers[1].name, QStringLiteral("another view.tif"));
    }

    TEST_F(ImageLayerLoaderTestClass, RejectsMissingPosition) {
        positions.pop_back();
        EXPECT_THROW(buildImageLayers(tiles, positions), std::invalid_argument);
    }

    TEST_F(ImageLayerLoaderTestClass, RejectsDuplicateTileIds) {
        // Deux entrées avec le même identifiant, chacune représentée par une fiche distincte.
        tiles.push_back(std::make_shared<DataNs::TileClass>(*tiles.front()));
        EXPECT_THROW(buildImageLayers(tiles, positions), std::invalid_argument);
    }

    TEST_F(ImageLayerLoaderTestClass, RejectsDuplicatePositionIds) {
        positions.push_back({FIRST_TILE_ID, {20.0, 24.0}, 0.0});
        EXPECT_THROW(buildImageLayers(tiles, positions), std::invalid_argument);
    }

    TEST_F(ImageLayerLoaderTestClass, RejectsPositionWithoutMatchingTile) {
        tiles.pop_back();
        EXPECT_THROW(buildImageLayers(tiles, positions), std::invalid_argument);
    }

    TEST_F(ImageLayerLoaderTestClass, SkipsRotatedTileAndLoadsFollowingTile) {
        positions.front().rotationRadians = 0.25;
        QRectF bounds;
        const auto layers = buildImageLayers(tiles, positions, 1, &bounds);
        EXPECT_EQ(bounds, QRectF(6, -2, 30, 22));

        ASSERT_EQ(layers.size(), 1);
        EXPECT_EQ(layers.front().id, QString::number(SECOND_TILE_ID));
        EXPECT_EQ(layers.front().worldRect, QRectF(20, 8, IMAGE_WIDTH / 2.0, IMAGE_HEIGHT / 2.0));
        EXPECT_FALSE(layers.front().image.isNull());
    }

}
