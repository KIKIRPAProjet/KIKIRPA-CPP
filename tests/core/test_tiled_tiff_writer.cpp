// tests/core/test_tiled_tiff_writer.cpp
#include "core/io/tiled_tiff_writer.h"

#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

    // 3 × 2 blocs, dont une colonne et une ligne de bord incomplètes.
    constexpr int CANVAS_WIDTH = 2 * DataNs::BLOCK_SIZE + 76;
    constexpr int CANVAS_HEIGHT = DataNs::BLOCK_SIZE + 188;
    // Au-delà de 4 Gio en 16 bits RGB : le writer doit passer en BigTIFF.
    constexpr int BIG_TIFF_CANVAS_SIDE = 40000;
    constexpr int PIXEL_VALUE_MODULO = 251;

    cv::Size testCanvasSize() { return {CANVAS_WIDTH, CANVAS_HEIGHT}; }

    class TemporaryDirectoryClass {
        public:
            TemporaryDirectoryClass() {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("kikirpa-tiled-writer-" + std::to_string(uniqueValue));
                fs::create_directories(path);
            }

            ~TemporaryDirectoryClass() {
                std::error_code errorCode;
                fs::remove_all(path, errorCode);
            }

            TemporaryDirectoryClass(const TemporaryDirectoryClass&) = delete;
            TemporaryDirectoryClass& operator=(const TemporaryDirectoryClass&) = delete;

            fs::path path;
    };

    // Image de référence : chaque pixel a une valeur différente de ses voisins, pour détecter un bloc mal placé.
    cv::Mat makeReferenceImage(cv::Size size, int type) {
        cv::Mat image(size, type);
        const int channelCount = image.channels();
        for (int row = 0; row < size.height; ++row)
            for (int column = 0; column < size.width; ++column)
                for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                    const int value = (row * 7 + column * 3 + channelIndex * 11) % PIXEL_VALUE_MODULO;
                    if (image.depth() == CV_8U)
                        image.ptr<std::uint8_t>(row)[column * channelCount + channelIndex] = static_cast<std::uint8_t>(value);
                    else
                        image.ptr<std::uint16_t>(row)[column * channelCount + channelIndex] =
                            static_cast<std::uint16_t>(value * PIXEL_VALUE_MODULO);
                }
        return image;
    }

    std::vector<cv::Rect> blockRectangles(cv::Size canvasSize) {
        std::vector<cv::Rect> rectangles;
        for (int blockY = 0; blockY < canvasSize.height; blockY += DataNs::BLOCK_SIZE)
            for (int blockX = 0; blockX < canvasSize.width; blockX += DataNs::BLOCK_SIZE)
                rectangles.emplace_back(blockX, blockY, std::min(DataNs::BLOCK_SIZE, canvasSize.width - blockX),
                                        std::min(DataNs::BLOCK_SIZE, canvasSize.height - blockY));
        return rectangles;
    }

    bool writeBlocks(IONs::TiledTiffWriterClass& writer, const cv::Mat& image,
                     const std::vector<cv::Rect>& rectangles, std::string& errorMessage) {
        for (const cv::Rect& rectangle : rectangles)
            if (!writer.writeBlock(rectangle.tl(), image(rectangle), errorMessage)) return false;
        return true;
    }

    cv::Mat readWholeImage(const fs::path& path, cv::Size canvasSize) {
        cv::Mat image;
        std::string errorMessage;
        const IONs::PyramidalTiffReaderClass reader(path);
        EXPECT_TRUE(reader.readRegion(0, cv::Rect({0, 0}, canvasSize), image, errorMessage)) << errorMessage;
        return image;
    }

    std::vector<std::string> fileNames(const fs::path& directory) {
        std::vector<std::string> names;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory))
            names.push_back(entry.path().filename().string());
        return names;
    }

    std::string readText(const fs::path& path) {
        std::ifstream input(path);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    bool areEqual(const cv::Mat& first, const cv::Mat& second) {
        return first.size() == second.size() && first.type() == second.type() &&
               cv::norm(first, second, cv::NORM_INF) == 0.0;
    }

    class TiledTiffWriterTypeTestClass : public testing::TestWithParam<int> {};

}

// Les blocs arrivent dans n'importe quel ordre ; ceux du bord sont plus petits. Relu, le TIFF redonne l'image.
TEST_P(TiledTiffWriterTypeTestClass, WritesBlocksInAnyOrderAndReadsThemBack) {
    const TemporaryDirectoryClass temporaryDirectory;
    const fs::path outputPath = temporaryDirectory.path / "image.tif";
    const cv::Mat reference = makeReferenceImage(testCanvasSize(), GetParam());
    std::vector<cv::Rect> rectangles = blockRectangles(testCanvasSize());
    std::reverse(rectangles.begin(), rectangles.end());
    std::swap(rectangles.front(), rectangles[2]);

    IONs::TiledTiffWriterClass writer;
    std::string errorMessage;
    ASSERT_TRUE(writer.open(outputPath, testCanvasSize(), GetParam(), errorMessage)) << errorMessage;
    ASSERT_TRUE(writeBlocks(writer, reference, rectangles, errorMessage)) << errorMessage;
    ASSERT_TRUE(writer.close(errorMessage)) << errorMessage;

    EXPECT_FALSE(writer.isBigTiff());
    EXPECT_TRUE(areEqual(readWholeImage(outputPath, testCanvasSize()), reference));
    EXPECT_EQ(fileNames(temporaryDirectory.path), std::vector<std::string>{"image.tif"});
}

INSTANTIATE_TEST_SUITE_P(SupportedTypes, TiledTiffWriterTypeTestClass, testing::Values(CV_16UC3, CV_16UC1, CV_8UC1));

TEST(TiledTiffWriterTest, CloseFailsWhenABlockIsMissingAndLeavesNoFile) {
    const TemporaryDirectoryClass temporaryDirectory;
    const cv::Mat reference = makeReferenceImage(testCanvasSize(), CV_16UC3);
    std::vector<cv::Rect> rectangles = blockRectangles(testCanvasSize());
    rectangles.pop_back();

    IONs::TiledTiffWriterClass writer;
    std::string errorMessage;
    ASSERT_TRUE(writer.open(temporaryDirectory.path / "image.tif", testCanvasSize(), CV_16UC3, errorMessage));
    ASSERT_TRUE(writeBlocks(writer, reference, rectangles, errorMessage)) << errorMessage;

    EXPECT_FALSE(writer.close(errorMessage));
    EXPECT_NE(errorMessage.find("not written"), std::string::npos) << errorMessage;
    EXPECT_TRUE(fileNames(temporaryDirectory.path).empty());
}

// Sans close() (erreur en cours d'écriture), le destructeur supprime le temporaire et garde l'ancien fichier.
TEST(TiledTiffWriterTest, DestructionWithoutCloseKeepsThePreviousFile) {
    const TemporaryDirectoryClass temporaryDirectory;
    const fs::path outputPath = temporaryDirectory.path / "image.tif";
    std::ofstream(outputPath) << "previous output";
    {
        IONs::TiledTiffWriterClass writer;
        std::string errorMessage;
        ASSERT_TRUE(writer.open(outputPath, testCanvasSize(), CV_8UC1, errorMessage)) << errorMessage;
        const cv::Mat reference = makeReferenceImage(testCanvasSize(), CV_8UC1);
        ASSERT_TRUE(writeBlocks(writer, reference, {cv::Rect(0, 0, DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE)},
                                errorMessage));
    }
    EXPECT_EQ(fileNames(temporaryDirectory.path), std::vector<std::string>{"image.tif"});
    EXPECT_EQ(readText(outputPath), "previous output");
}

TEST(TiledTiffWriterTest, RejectsMisalignedOrMissizedBlocks) {
    const TemporaryDirectoryClass temporaryDirectory;
    IONs::TiledTiffWriterClass writer;
    std::string errorMessage;
    ASSERT_TRUE(writer.open(temporaryDirectory.path / "image.tif", testCanvasSize(), CV_16UC1, errorMessage));
    const cv::Mat fullBlock(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, CV_16UC1, cv::Scalar(1));

    EXPECT_FALSE(writer.writeBlock({1, 0}, fullBlock, errorMessage));
    EXPECT_FALSE(writer.writeBlock({0, CANVAS_HEIGHT}, fullBlock, errorMessage));
    // Le dernier bloc de la ligne est un bloc de bord : un bloc complet n'y a pas la bonne taille.
    EXPECT_FALSE(writer.writeBlock({2 * DataNs::BLOCK_SIZE, 0}, fullBlock, errorMessage));
    EXPECT_FALSE(writer.writeBlock({0, 0}, cv::Mat(fullBlock.size(), CV_8UC1, cv::Scalar(1)), errorMessage));
}

TEST(TiledTiffWriterTest, RejectsABlockWrittenTwice) {
    const TemporaryDirectoryClass temporaryDirectory;
    IONs::TiledTiffWriterClass writer;
    std::string errorMessage;
    ASSERT_TRUE(writer.open(temporaryDirectory.path / "image.tif", testCanvasSize(), CV_16UC1, errorMessage));
    const cv::Mat fullBlock(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, CV_16UC1, cv::Scalar(1));

    ASSERT_TRUE(writer.writeBlock({0, 0}, fullBlock, errorMessage)) << errorMessage;
    EXPECT_FALSE(writer.writeBlock({0, 0}, fullBlock, errorMessage));
    EXPECT_NE(errorMessage.find("already written"), std::string::npos) << errorMessage;
}

TEST(TiledTiffWriterTest, RejectsUnsupportedTypesAndEmptyImages) {
    const TemporaryDirectoryClass temporaryDirectory;
    IONs::TiledTiffWriterClass writer;
    std::string errorMessage;

    EXPECT_FALSE(writer.open(temporaryDirectory.path / "image.tif", testCanvasSize(), CV_32FC1, errorMessage));
    EXPECT_FALSE(writer.open(temporaryDirectory.path / "image.tif", testCanvasSize(), CV_16UC4, errorMessage));
    EXPECT_FALSE(writer.open(temporaryDirectory.path / "image.tif", {0, 1}, CV_16UC3, errorMessage));
    EXPECT_TRUE(fileNames(temporaryDirectory.path).empty());
}

// Seuls l'en-tête et le choix du format sont vérifiés : écrire 9,6 Gio de pixels ne tient pas dans un test.
TEST(TiledTiffWriterTest, ChoosesBigTiffForLargeImages) {
    const TemporaryDirectoryClass temporaryDirectory;
    std::string errorMessage;
    {
        IONs::TiledTiffWriterClass writer;
        ASSERT_TRUE(writer.open(temporaryDirectory.path / "image.tif", cv::Size(BIG_TIFF_CANVAS_SIDE, BIG_TIFF_CANVAS_SIDE), CV_16UC3, errorMessage))
            << errorMessage;
        EXPECT_TRUE(writer.isBigTiff());
    }
    EXPECT_TRUE(fileNames(temporaryDirectory.path).empty());
}
