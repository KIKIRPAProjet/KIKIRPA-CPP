// tests/core/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using Json = StepsNs::Json;

namespace {

    // Deux blocs : un complet et un bloc de bord.
    constexpr int CANVAS_WIDTH = DataNs::BLOCK_SIZE + 88;
    constexpr int CANVAS_HEIGHT = 300;
    constexpr double LEFT_VALUE = 100.0;
    constexpr double RIGHT_VALUE = 3000.0;

    cv::Size testCanvasSize() { return {CANVAS_WIDTH, CANVAS_HEIGHT}; }

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

            TemporaryDirectoryClass(const TemporaryDirectoryClass&) = delete;
            TemporaryDirectoryClass& operator=(const TemporaryDirectoryClass&) = delete;

            fs::path path;
    };

    // Image de référence : le premier bloc d'une couleur, le bloc de bord d'une autre.
    cv::Mat makeReferenceImage(int type) {
        // Canaux différents : une inversion RGB / BGR se verrait.
        cv::Mat image(testCanvasSize(), type, cv::Scalar(LEFT_VALUE, 2 * LEFT_VALUE, 3 * LEFT_VALUE));
        image(cv::Rect(DataNs::BLOCK_SIZE, 0, CANVAS_WIDTH - DataNs::BLOCK_SIZE, CANVAS_HEIGHT))
            .setTo(cv::Scalar(RIGHT_VALUE, 2 * RIGHT_VALUE, 3 * RIGHT_VALUE));
        return image;
    }

    // Écrit l'image comme le ferait l'assemblage : bloc par bloc, avec le writer partagé.
    void writeTiledImage(const fs::path& path, const cv::Mat& image) {
        IONs::TiledTiffWriterClass writer;
        std::string errorMessage;
        ASSERT_TRUE(writer.open(path, image.size(), image.type(), errorMessage)) << errorMessage;
        for (int blockX = 0; blockX < image.cols; blockX += DataNs::BLOCK_SIZE) {
            const cv::Rect blockRect(blockX, 0, std::min(DataNs::BLOCK_SIZE, image.cols - blockX), image.rows);
            ASSERT_TRUE(writer.writeBlock(blockRect.tl(), image(blockRect), errorMessage)) << errorMessage;
        }
        ASSERT_TRUE(writer.close(errorMessage)) << errorMessage;
    }

    DataNs::Mosaic makeMosaic(const fs::path& directory, int imageType) {
        DataNs::Mosaic mosaic;
        mosaic.imagePath = directory / "mosaic_image.tif";
        mosaic.canvasSize = testCanvasSize();
        writeTiledImage(mosaic.imagePath, makeReferenceImage(imageType));
        return mosaic;
    }

    void configureOutputDirectory(StepsNs::ExportStepClass& step, const fs::path& directory) {
        const Json config = {{"name", "export"},
                             {"parameters", Json::array({Json{{"valueType", "string"},
                                                               {"name", "outputDirectory"},
                                                               {"value", directory.u8string()}}})}};
        step.importConfig(config);
    }

    std::vector<std::string> fileNames(const fs::path& directory) {
        std::vector<std::string> names;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory))
            names.push_back(entry.path().filename().string());
        return names;
    }

    struct ExportFixture {
        TemporaryDirectoryClass temporaryDirectory;
        fs::path fusionDirectory = temporaryDirectory.path / "fusion";
        fs::path outputDirectory = temporaryDirectory.path / "output";
        StepNs::DataStoreClass dataStore;
        StepsNs::ExportStepClass step;

        explicit ExportFixture(int imageType) {
            dataStore.replace("mosaic", makeMosaic(fusionDirectory, imageType));
            configureOutputDirectory(step, outputDirectory);
            step.setDataStore(dataStore);
        }
    };

}

TEST(ExportStepTest, PublishesOnlyTheMosaicImage) {
    ExportFixture fixture(CV_16UC3);
    fs::create_directories(fixture.outputDirectory);
    std::ofstream(fixture.outputDirectory / "mosaic.tif") << "old output";

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const StepsNs::ExportOutput& output = fixture.step.getOutput();
    EXPECT_EQ(output.imagePath, fixture.outputDirectory / "mosaic.tif");
    EXPECT_EQ(output.canvasSize, testCanvasSize());
    EXPECT_FALSE(output.isBigTiff);

    cv::Mat published;
    std::string errorMessage;
    ASSERT_TRUE(IONs::PyramidalTiffReaderClass(output.imagePath)
                    .readRegion(0, cv::Rect({0, 0}, testCanvasSize()), published, errorMessage)) << errorMessage;
    EXPECT_EQ(cv::norm(published, makeReferenceImage(CV_16UC3), cv::NORM_INF), 0.0);
    EXPECT_EQ(fileNames(fixture.outputDirectory), std::vector<std::string>{"mosaic.tif"});
    // La sortie de l'assemblage reste en place : l'export peut être refait sans refaire l'assemblage.
    EXPECT_TRUE(fs::exists(fixture.dataStore.get<DataNs::Mosaic>("mosaic").imagePath));
}

TEST(ExportStepTest, RejectsAnImageThatDoesNotMatchTheCanvasSize) {
    ExportFixture fixture(CV_16UC3);
    DataNs::Mosaic mosaic = fixture.dataStore.get<DataNs::Mosaic>("mosaic");
    mosaic.canvasSize.width += 1;
    fixture.dataStore.replace("mosaic", mosaic);

    EXPECT_FALSE(fixture.step.execute());
    EXPECT_NE(fixture.step.getResult().value("error", "").find("canvas size"), std::string::npos);
    EXPECT_FALSE(fs::exists(fixture.outputDirectory / "mosaic.tif"));
}

TEST(ExportStepTest, RejectsAnImageThatIsNotRgb16) {
    ExportFixture fixture(CV_16UC1);

    EXPECT_FALSE(fixture.step.execute());
    EXPECT_NE(fixture.step.getResult().value("error", "").find("16-bit RGB"), std::string::npos);
    EXPECT_FALSE(fs::exists(fixture.outputDirectory / "mosaic.tif"));
}

TEST(ExportStepTest, ReportsAMissingMosaicImage) {
    ExportFixture fixture(CV_16UC3);
    fs::remove(fixture.dataStore.get<DataNs::Mosaic>("mosaic").imagePath);

    EXPECT_FALSE(fixture.step.execute());
    EXPECT_NE(fixture.step.getResult().value("error", "").find("cannot open mosaic image"), std::string::npos);
}

TEST(ExportStepTest, ReportsAMissingMosaicInput) {
    StepsNs::ExportStepClass step;
    StepNs::DataStoreClass dataStore;
    step.setDataStore(dataStore);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult().value("error", "").find("mosaic"), std::string::npos);
}

// L'empreinte suit mosaic.tif : le checkpoint de l'export se périme si le fichier publié disparaît.
TEST(ExportStepTest, FingerprintFollowsThePublishedFile) {
    ExportFixture fixture(CV_16UC3);
    EXPECT_FALSE(fixture.step.externalInputsFingerprint().at("exists").get<bool>());

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    EXPECT_TRUE(fixture.step.externalInputsFingerprint().at("exists").get<bool>());
}
