// tests/core/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/io/unicode_path.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    constexpr int JPEG_PREVIEW_SIDE = 300;
    constexpr int PIXEL_TOLERANCE = 5;
    constexpr double SIXTEEN_TO_EIGHT_BIT_DIVISOR = 257.0;
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

    void configureOutput(StepsNs::ExportStepClass& step, const fs::path& directory, const std::string& filename) {
        Json parameters = Json::array({Json{{"valueType", "string"},
                                            {"name", "outputDirectory"},
                                            {"value", directory.u8string()}}});
        if (!filename.empty()) {
            parameters.push_back(Json{{"valueType", "string"},
                                      {"name", "filename"},
                                      {"value", filename}});
        }
        const Json config = {{"name", "export"}, {"parameters", parameters}};
        step.importConfig(config);
    }

    void configureJpegExport(StepsNs::ExportStepClass& step, const fs::path& directory) {
        const Json config = {{"name", "export"},
                             {"parameters", Json::array({Json{{"valueType", "string"},
                                                               {"name", "outputDirectory"},
                                                               {"value", directory.u8string()}},
                                                          Json{{"valueType", "string"},
                                                               {"name", "filename"},
                                                               {"value", "mosaic.jpg"}},
                                                          Json{{"valueType", "string"},
                                                               {"name", "format"},
                                                               {"value", "jpeg"}},
                                                          Json{{"valueType", "int"},
                                                               {"name", "previewMaxSide"},
                                                               {"value", 300}}})}};
        step.importConfig(config);
    }

    void configureBothExport(StepsNs::ExportStepClass& step, const fs::path& directory) {
        const Json config = {{"name", "export"},
                             {"parameters", Json::array({Json{{"valueType", "string"},
                                                               {"name", "outputDirectory"},
                                                               {"value", directory.u8string()}},
                                                          Json{{"valueType", "string"},
                                                               {"name", "filename"},
                                                               {"value", "mosaic.tif"}},
                                                          Json{{"valueType", "string"},
                                                               {"name", "format"},
                                                               {"value", "both"}},
                                                          Json{{"valueType", "int"},
                                                               {"name", "previewMaxSide"},
                                                               {"value", 300}}})}};
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

TEST(ExportStepTest, IncrementsFilenameWhenFileAlreadyExists) {
    ExportFixture fixture(CV_16UC3);
    fs::create_directories(fixture.outputDirectory);
    std::ofstream(fixture.outputDirectory / "mosaic.tif") << "old_output";

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const StepsNs::ExportOutput& output = fixture.step.getOutput();
    EXPECT_EQ(output.imagePath, fixture.outputDirectory / "mosaic (1).tif");
    EXPECT_EQ(output.canvasSize, testCanvasSize());

    // Vérifie que l'ancien fichier est conservé et n'a pas été écrasé
    std::string oldContent;
    std::ifstream(fixture.outputDirectory / "mosaic.tif") >> oldContent;
    EXPECT_EQ(oldContent, "old_output");

    std::vector<std::string> names = fileNames(fixture.outputDirectory);
    std::sort(names.begin(), names.end());
    const std::vector<std::string> expectedNames = {"mosaic (1).tif", "mosaic.tif"};
    EXPECT_EQ(names, expectedNames);

    // Une exécution suivante incrémente vers (2)
    StepsNs::ExportStepClass secondStep;
    configureOutputDirectory(secondStep, fixture.outputDirectory);
    secondStep.setDataStore(fixture.dataStore);
    ASSERT_TRUE(secondStep.execute()) << secondStep.getResult().dump();
    EXPECT_EQ(secondStep.getOutput().imagePath, fixture.outputDirectory / "mosaic (2).tif");
}

// Un nouvel export suit le numéro le plus élevé présent, même si des copies intermédiaires ont été supprimées : le
// dernier export est toujours celui de rang maximal.
TEST(ExportStepTest, NewExportFollowsTheHighestExistingNumber) {
    ExportFixture fixture(CV_16UC3);
    fs::create_directories(fixture.outputDirectory);
    std::ofstream(fixture.outputDirectory / "mosaic.tif") << "old_output";
    std::ofstream(fixture.outputDirectory / "mosaic (3).tif") << "old_output";
    std::ofstream(fixture.outputDirectory / "mosaic (x).tif") << "not_a_copy";

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    EXPECT_EQ(fixture.step.getOutput().imagePath, fixture.outputDirectory / "mosaic (4).tif");
}

// Reprise d'un run : un step neuf, avant execute(), retrouve le dernier export sur le disque. Son empreinte égale celle
// enregistrée à la sauvegarde du checkpoint, qui reste donc valable (pas de copie de plus) ; si le fichier exporté a
// disparu, elle change et l'export est refait.
TEST(ExportStepTest, ResumedStepKeepsItsCheckpointWhileTheExportIsUnchanged) {
    ExportFixture fixture(CV_16UC3);
    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const Json savedFingerprint = fixture.step.externalInputsFingerprint();

    StepsNs::ExportStepClass resumedStep;
    configureOutputDirectory(resumedStep, fixture.outputDirectory);
    EXPECT_EQ(resumedStep.externalInputsFingerprint(), savedFingerprint);

    fs::remove(fixture.step.getOutput().imagePath);
    EXPECT_NE(resumedStep.externalInputsFingerprint(), savedFingerprint);
}

TEST(ExportStepTest, SupportsCustomFilename) {
    ExportFixture fixture(CV_16UC3);
    configureOutput(fixture.step, fixture.outputDirectory, "custom_artwork.tif");

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    EXPECT_EQ(fixture.step.getOutput().imagePath, fixture.outputDirectory / "custom_artwork.tif");

    // En cas de ré-exécution avec le même nom personnalisé, il incrémente proprement
    StepsNs::ExportStepClass secondStep;
    configureOutput(secondStep, fixture.outputDirectory, "custom_artwork.tif");
    secondStep.setDataStore(fixture.dataStore);
    ASSERT_TRUE(secondStep.execute()) << secondStep.getResult().dump();
    EXPECT_EQ(secondStep.getOutput().imagePath, fixture.outputDirectory / "custom_artwork (1).tif");
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

TEST(ExportStepTest, PublishesJpegPreviewAsEightBitImage) {
    ExportFixture fixture(CV_16UC3);
    configureJpegExport(fixture.step, fixture.outputDirectory);

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const StepsNs::ExportOutput& output = fixture.step.getOutput();
    EXPECT_EQ(output.imagePath, fixture.outputDirectory / "mosaic.jpg");
    EXPECT_EQ(output.format, "jpeg");

    const cv::Mat published = IONs::readImage(output.imagePath, cv::IMREAD_COLOR);
    ASSERT_FALSE(published.empty());
    EXPECT_EQ(published.type(), CV_8UC3);
    EXPECT_LE(published.cols, JPEG_PREVIEW_SIDE);
    EXPECT_LE(published.rows, JPEG_PREVIEW_SIDE);
    
    // Le pixel (0,0) subit une division par 257.0 pour passer de 16-bit à 8-bit.
    // L'export JPEG convertit également de RGB (ordre initial) vers BGR (OpenCV)
    const cv::Vec3b& pixel = published.at<cv::Vec3b>(0, 0);
    EXPECT_NEAR(pixel[0], static_cast<int>(std::round((3 * LEFT_VALUE) / SIXTEEN_TO_EIGHT_BIT_DIVISOR)), PIXEL_TOLERANCE); // Canal Bleu (ancien Rouge)
    EXPECT_NEAR(pixel[1], static_cast<int>(std::round((2 * LEFT_VALUE) / SIXTEEN_TO_EIGHT_BIT_DIVISOR)), PIXEL_TOLERANCE); // Canal Vert (inchangé)
    EXPECT_NEAR(pixel[2], static_cast<int>(std::round(LEFT_VALUE / SIXTEEN_TO_EIGHT_BIT_DIVISOR)), PIXEL_TOLERANCE);       // Canal Rouge (ancien Bleu)
}

// L'empreinte suit le fichier publié : le checkpoint de l'export se périme si ce fichier disparaît.
TEST(ExportStepTest, FingerprintFollowsThePublishedFile) {
    ExportFixture fixture(CV_16UC3);

    EXPECT_TRUE(fixture.step.externalInputsFingerprint().is_null());

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const auto fingerprint = fixture.step.externalInputsFingerprint();
    ASSERT_TRUE(fingerprint.is_object());
    ASSERT_TRUE(fingerprint.contains("files"));
    ASSERT_EQ(fingerprint.at("files").size(), 1);
    EXPECT_TRUE(fingerprint.at("files").at(0).at("exists").get<bool>());
}

TEST(ExportStepTest, PublishesTiffAndJpegPreviewInBothMode) {
    ExportFixture fixture(CV_16UC3);
    configureBothExport(fixture.step, fixture.outputDirectory);

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    EXPECT_EQ(fixture.step.getOutput().format, "both");
    EXPECT_TRUE(fs::exists(fixture.outputDirectory / "mosaic.tif"));
    EXPECT_TRUE(fs::exists(fixture.outputDirectory / "mosaic.jpg"));
}

TEST(ExportStepTest, MissingJpegInBothModeInvalidatesFingerprint) {
    ExportFixture fixture(CV_16UC3);
    configureBothExport(fixture.step, fixture.outputDirectory);

    ASSERT_TRUE(fixture.step.execute()) << fixture.step.getResult().dump();
    const Json savedFingerprint = fixture.step.externalInputsFingerprint();

    fs::remove(fixture.outputDirectory / "mosaic.jpg");

    StepsNs::ExportStepClass resumedStep;
    configureBothExport(resumedStep, fixture.outputDirectory);
    resumedStep.setDataStore(fixture.dataStore);
    EXPECT_NE(resumedStep.externalInputsFingerprint(), savedFingerprint);
    ASSERT_TRUE(resumedStep.execute()) << "resume export failed: " << resumedStep.getResult().dump();
    EXPECT_TRUE(fs::exists(fixture.outputDirectory / "mosaic.jpg"));
}
