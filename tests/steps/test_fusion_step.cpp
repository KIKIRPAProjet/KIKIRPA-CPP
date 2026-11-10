// tests/steps/test_fusion_step.cpp
#include "steps/fusion_step.h"

#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"
#include "steps/export_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using Json = StepsNs::Json;

namespace {

    // Deux tuiles de 64 × 48 côte à côte, la seconde décalée de 40 pixels : la couture passe à x = 50.
    constexpr int SMALL_TILE_WIDTH = 64;
    constexpr int SMALL_TILE_HEIGHT = 48;
    constexpr int SMALL_TILE_SHIFT = 40;
    constexpr int SMALL_SEAM_X = 50;
    // Deux tuiles de 400 × 600, la seconde décalée de 300 : mosaïque de 700 × 600, soit 2 × 2 blocs dont des bords.
    constexpr int LARGE_TILE_WIDTH = 400;
    constexpr int LARGE_TILE_HEIGHT = 600;
    constexpr int LARGE_TILE_SHIFT = 300;
    constexpr int LARGE_SEAM_X = 350;
    constexpr int LARGE_BLOCK_COUNT = 4;
    // Identifiants volontairement différents des indices dans `tiles`.
    constexpr int LEFT_TILE_ID = 5;
    constexpr int RIGHT_TILE_ID = 9;
    // Valeurs des pixels : B = 10 x + 1, G = 10 y + 2, R = 1000 (id + 1) dans la tuile (ordre BGR d'OpenCV).
    constexpr int COORDINATE_FACTOR = 10;
    constexpr int TILE_ID_FACTOR = 1000;
    constexpr int VALID_PIXEL = 255;
    constexpr double LEFT_GAIN = 2.0;
    constexpr int EIGHT_BIT_VALUE = 120;
    constexpr int EIGHT_TO_SIXTEEN_BIT_FACTOR = 257;
    // Translation commune qui rend toutes les positions négatives.
    constexpr double NEGATIVE_OFFSET_X = -1000.0;
    constexpr double NEGATIVE_OFFSET_Y = -20.0;

    class TemporaryDirectoryClass {
        public:
            TemporaryDirectoryClass() {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("kikirpa-fusion-test-" + std::to_string(uniqueValue));
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

    std::uint16_t toPixelValue(int value) { return static_cast<std::uint16_t>(value); }

    // Pixel attendu dans la mosaïque (RGB) pour le pixel (x, y) de la tuile `tileId`.
    cv::Vec3w expectedRgb(int tileId, int tileX, int tileY) {
        return {toPixelValue(TILE_ID_FACTOR * (tileId + 1)), toPixelValue(COORDINATE_FACTOR * tileY + 2),
                toPixelValue(COORDINATE_FACTOR * tileX + 1)};
    }

    cv::Mat makePatternImage(int tileId, cv::Size size) {
        cv::Mat image(size, CV_16UC3);
        for (int row = 0; row < size.height; ++row)
            for (int column = 0; column < size.width; ++column) {
                const cv::Vec3w rgb = expectedRgb(tileId, column, row);
                image.at<cv::Vec3w>(row, column) = cv::Vec3w(rgb[2], rgb[1], rgb[0]);   // BGR, comme cv::imread
            }
        return image;
    }

    std::string tileStem(int column) { return "tile_r00_c0" + std::to_string(column); }

    // Tuile comme après l'import : TIFF pyramidal écrit, fiche sans pixel. L'image source n'a pas besoin d'exister.
    std::shared_ptr<DataNs::TileClass> makeTile(const fs::path& directory, int tileId, int column, const cv::Mat& image) {
        const fs::path tiffPath = directory / (tileStem(column) + "_pyramid.tif");
        std::string errorMessage;
        if (!IONs::createPyramidalTiledTiff(image, tiffPath, errorMessage)) throw std::runtime_error(errorMessage);
        return std::make_shared<DataNs::TileClass>(tileId, directory / (tileStem(column) + ".tif"), tiffPath,
                                                   image.cols, image.rows, image.type());
    }

    // Territoire rectangulaire, avec le contour que donnerait findContours (sommets sur les pixels du bord).
    DataNs::TileTerritory makeRectangleTerritory(int tileIndex, const cv::Rect& rectangle) {
        const int right = rectangle.x + rectangle.width - 1;
        const int bottom = rectangle.y + rectangle.height - 1;
        return {tileIndex, {{{rectangle.x, rectangle.y}, {right, rectangle.y}, {right, bottom}, {rectangle.x, bottom}}}, {{}}};
    }

    struct TwoTileScene {
        cv::Size tileSize;
        int shift = 0;      // décalage de la tuile de droite
        int seamX = 0;      // première colonne de la tuile de droite dans la mosaïque
        cv::Point2d origin; // position de la tuile de gauche
    };

    TwoTileScene smallScene() { return {{SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT}, SMALL_TILE_SHIFT, SMALL_SEAM_X, {0, 0}}; }

    // Remplit le DataStore comme le feraient import, absolute-pos et graphcut.
    void fillTwoTileStore(StepNs::DataStoreClass& dataStore, const fs::path& tileDirectory, const TwoTileScene& scene) {
        dataStore.replace("tiles", DataNs::TileList{
                                       makeTile(tileDirectory, LEFT_TILE_ID, 0, makePatternImage(LEFT_TILE_ID, scene.tileSize)),
                                       makeTile(tileDirectory, RIGHT_TILE_ID, 1, makePatternImage(RIGHT_TILE_ID, scene.tileSize))});
        dataStore.replace("positions", std::vector<DataNs::TilePositionData>{
                                           {"r00_c00", scene.origin, 0.0},
                                           {"r00_c01", scene.origin + cv::Point2d(scene.shift, 0), 0.0}});
        const int mosaicWidth = scene.shift + scene.tileSize.width;
        dataStore.replace("territories", std::vector<DataNs::TileTerritory>{
                                             makeRectangleTerritory(0, {0, 0, scene.seamX, scene.tileSize.height}),
                                             makeRectangleTerritory(1, {scene.seamX, 0, mosaicWidth - scene.seamX,
                                                                        scene.tileSize.height})});
    }

    StepsNs::FusionStepClass makeFusionStep(const fs::path& outputDirectory, StepNs::DataStoreClass& dataStore) {
        StepsNs::FusionStepClass step;
        step.importConfig({{"name", "fusion"},
                           {"parameters", Json::array({Json{{"valueType", "string"}, {"name", "outputDirectory"},
                                                            {"value", outputDirectory.u8string()}}})}});
        step.setDataStore(dataStore);
        return step;
    }

    cv::Mat readImage(const fs::path& path, cv::Size size) {
        cv::Mat image;
        std::string errorMessage;
        if (!IONs::PyramidalTiffReaderClass(path).readRegion(0, cv::Rect({0, 0}, size), image, errorMessage))
            throw std::runtime_error(errorMessage);
        return image;
    }

    struct MosaicImages {
        cv::Mat imageRgb;
        cv::Mat provenance;
        cv::Mat mask;
    };

    MosaicImages readMosaic(const DataNs::Mosaic& mosaic) {
        return {readImage(mosaic.imagePath, mosaic.canvasSize), readImage(mosaic.provenancePath, mosaic.canvasSize),
                readImage(mosaic.maskPath, mosaic.canvasSize)};
    }

    bool areEqual(const cv::Mat& first, const cv::Mat& second) {
        return first.size() == second.size() && first.type() == second.type() &&
               cv::norm(first, second, cv::NORM_INF) == 0.0;
    }

    // Exécute fusion sur une scène à deux tuiles et relit la mosaïque produite.
    MosaicImages runTwoTileFusion(const fs::path& directory, const TwoTileScene& scene) {
        StepNs::DataStoreClass dataStore;
        fillTwoTileStore(dataStore, directory / "tiles", scene);
        StepsNs::FusionStepClass step = makeFusionStep(directory / "fusion", dataStore);
        if (!step.execute()) throw std::runtime_error(step.getResult().dump());
        return readMosaic(dataStore.get<DataNs::Mosaic>("mosaic"));
    }

}

// Des deux côtés de la couture : pixels de la bonne tuile, au bon endroit, en RGB ; provenance = identifiant de tuile.
TEST(FusionStepTest, TakesEachPixelFromTheTileOfItsTerritory) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const DataNs::Mosaic& mosaic = dataStore.get<DataNs::Mosaic>("mosaic");
    EXPECT_EQ(mosaic.canvasSize, cv::Size(SMALL_TILE_SHIFT + SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT));
    const MosaicImages images = readMosaic(mosaic);
    constexpr int ROW = 7;
    EXPECT_EQ(images.imageRgb.at<cv::Vec3w>(ROW, SMALL_SEAM_X - 1), expectedRgb(LEFT_TILE_ID, SMALL_SEAM_X - 1, ROW));
    EXPECT_EQ(images.imageRgb.at<cv::Vec3w>(ROW, SMALL_SEAM_X),
              expectedRgb(RIGHT_TILE_ID, SMALL_SEAM_X - SMALL_TILE_SHIFT, ROW));
    EXPECT_EQ(images.provenance.at<std::uint16_t>(ROW, SMALL_SEAM_X - 1), LEFT_TILE_ID);
    EXPECT_EQ(images.provenance.at<std::uint16_t>(ROW, SMALL_SEAM_X), RIGHT_TILE_ID);
    EXPECT_EQ(cv::countNonZero(images.mask != VALID_PIXEL), 0);
    EXPECT_EQ(step.getOutput().value.imagePath, mosaic.imagePath);
}

// absolute-pos peut donner des positions négatives : une translation commune ne change pas la mosaïque.
TEST(FusionStepTest, NegativePositionsGiveTheSameMosaic) {
    const TemporaryDirectoryClass temporaryDirectory;
    TwoTileScene shiftedScene = smallScene();
    shiftedScene.origin = {NEGATIVE_OFFSET_X, NEGATIVE_OFFSET_Y};

    const MosaicImages reference = runTwoTileFusion(temporaryDirectory.path / "reference", smallScene());
    const MosaicImages shifted = runTwoTileFusion(temporaryDirectory.path / "shifted", shiftedScene);

    EXPECT_TRUE(areEqual(shifted.imageRgb, reference.imageRgb));
    EXPECT_TRUE(areEqual(shifted.provenance, reference.provenance));
    EXPECT_TRUE(areEqual(shifted.mask, reference.mask));
}

// Mosaïque de plus d'un bloc : grille complète depuis (0, 0), blocs de bord compris, chaque pixel à sa place.
TEST(FusionStepTest, CoversTheWholeBlockGridOfALargeMosaic) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    const TwoTileScene scene{{LARGE_TILE_WIDTH, LARGE_TILE_HEIGHT}, LARGE_TILE_SHIFT, LARGE_SEAM_X, {0, 0}};
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", scene);
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    EXPECT_EQ(step.getResult().at("blockCount").get<int>(), LARGE_BLOCK_COUNT);
    const MosaicImages images = readMosaic(dataStore.get<DataNs::Mosaic>("mosaic"));
    const cv::Size canvasSize(LARGE_TILE_SHIFT + LARGE_TILE_WIDTH, LARGE_TILE_HEIGHT);
    ASSERT_EQ(images.imageRgb.size(), canvasSize);
    cv::Mat expectedImage(canvasSize, CV_16UC3);
    cv::Mat expectedProvenance(canvasSize, CV_16UC1);
    for (int row = 0; row < canvasSize.height; ++row)
        for (int column = 0; column < canvasSize.width; ++column) {
            const bool isLeft = column < LARGE_SEAM_X;
            expectedImage.at<cv::Vec3w>(row, column) = isLeft ? expectedRgb(LEFT_TILE_ID, column, row)
                                                              : expectedRgb(RIGHT_TILE_ID, column - LARGE_TILE_SHIFT, row);
            expectedProvenance.at<std::uint16_t>(row, column) = toPixelValue(isLeft ? LEFT_TILE_ID : RIGHT_TILE_ID);
        }
    EXPECT_TRUE(areEqual(images.imageRgb, expectedImage));
    EXPECT_TRUE(areEqual(images.provenance, expectedProvenance));
    EXPECT_EQ(cv::countNonZero(images.mask != VALID_PIXEL), 0);
}

// Règle de graphcut : le contour d'un trou appartient au territoire qui l'entoure, l'intérieur à l'autre tuile.
TEST(FusionStepTest, GivesTheHoleOutlineToTheSurroundingTerritory) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    TwoTileScene scene = smallScene();
    scene.shift = 0;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", scene);
    constexpr int HOLE_FIRST = 10;
    constexpr int HOLE_LAST = 20;
    DataNs::TileTerritory surrounding = makeRectangleTerritory(0, {0, 0, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT});
    surrounding.holes = {{{{HOLE_FIRST, HOLE_FIRST}, {HOLE_LAST, HOLE_FIRST}, {HOLE_LAST, HOLE_LAST}, {HOLE_FIRST, HOLE_LAST}}}};
    const DataNs::TileTerritory inside = makeRectangleTerritory(
        1, {HOLE_FIRST + 1, HOLE_FIRST + 1, HOLE_LAST - HOLE_FIRST - 1, HOLE_LAST - HOLE_FIRST - 1});
    dataStore.replace("territories", std::vector<DataNs::TileTerritory>{surrounding, inside});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const cv::Mat provenance = readMosaic(dataStore.get<DataNs::Mosaic>("mosaic")).provenance;
    constexpr int ROW = 15;
    EXPECT_EQ(provenance.at<std::uint16_t>(ROW, HOLE_FIRST), LEFT_TILE_ID);
    EXPECT_EQ(provenance.at<std::uint16_t>(ROW, HOLE_FIRST + 1), RIGHT_TILE_ID);
    EXPECT_EQ(provenance.at<std::uint16_t>(ROW, HOLE_LAST - 1), RIGHT_TILE_ID);
    EXPECT_EQ(provenance.at<std::uint16_t>(ROW, HOLE_LAST), LEFT_TILE_ID);
}

TEST(FusionStepTest, DividesPixelsByTheIntensityGain) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    dataStore.replace("intensityCorrection", DataNs::IntensityCorrection{{LEFT_GAIN, 1.0}, 0.0, {}});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const cv::Mat image = readMosaic(dataStore.get<DataNs::Mosaic>("mosaic")).imageRgb;
    constexpr int ROW = 3;
    constexpr int COLUMN = 4;
    const cv::Vec3w uncorrected = expectedRgb(LEFT_TILE_ID, COLUMN, ROW);
    for (int channelIndex = 0; channelIndex < 3; ++channelIndex)
        EXPECT_EQ(image.at<cv::Vec3w>(ROW, COLUMN)[channelIndex], std::round(uncorrected[channelIndex] / LEFT_GAIN));
    EXPECT_EQ(image.at<cv::Vec3w>(ROW, SMALL_SEAM_X), expectedRgb(RIGHT_TILE_ID, SMALL_SEAM_X - SMALL_TILE_SHIFT, ROW));
}

// Une tuile isolée (sans voisine exploitable pour intensity-correction) n'est pas corrigée.
TEST(FusionStepTest, LeavesIsolatedTilesUncorrected) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    dataStore.replace("intensityCorrection", DataNs::IntensityCorrection{{LEFT_GAIN, 1.0}, 0.0, {0}});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const cv::Mat image = readMosaic(dataStore.get<DataNs::Mosaic>("mosaic")).imageRgb;
    EXPECT_EQ(image.at<cv::Vec3w>(1, 1), expectedRgb(LEFT_TILE_ID, 1, 1));
}

// Les tuiles 8 bits sont acceptées et passent en 16 bits (× 257).
TEST(FusionStepTest, ConvertsEightBitTilesToSixteenBits) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    const cv::Mat eightBitImage(SMALL_TILE_HEIGHT, SMALL_TILE_WIDTH, CV_8UC3,
                                cv::Scalar(EIGHT_BIT_VALUE, EIGHT_BIT_VALUE, EIGHT_BIT_VALUE));
    const fs::path tileDirectory = temporaryDirectory.path / "tiles8";
    dataStore.replace("tiles", DataNs::TileList{makeTile(tileDirectory, LEFT_TILE_ID, 0, eightBitImage),
                                                makeTile(tileDirectory, RIGHT_TILE_ID, 1, eightBitImage)});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const cv::Mat image = readMosaic(dataStore.get<DataNs::Mosaic>("mosaic")).imageRgb;
    const auto sixteenBitValue = toPixelValue(EIGHT_BIT_VALUE * EIGHT_TO_SIXTEEN_BIT_FACTOR);
    EXPECT_EQ(image.at<cv::Vec3w>(0, 0), cv::Vec3w(sixteenBitValue, sixteenBitValue, sixteenBitValue));
}

TEST(FusionStepTest, FailsWithoutAPositionForEveryTileAndPublishesNoMosaic) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    dataStore.replace("positions", std::vector<DataNs::TilePositionData>{{"r00_c00", {0, 0}, 0.0}, {"r04_c04", {0, 0}, 0.0}});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult().value("error", "").find("no position for tile 'r00_c01'"), std::string::npos);
    EXPECT_TRUE(dataStore.get<DataNs::Mosaic>("mosaic").imagePath.empty());
    EXPECT_FALSE(fs::exists(temporaryDirectory.path / "fusion" / "mosaic_image.tif"));
}

TEST(FusionStepTest, FailsWhenATileNameHasNoGridPosition) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    const auto& tiles = dataStore.get<DataNs::TileList>("tiles");
    const DataNs::TileClass& left = *tiles[0];
    const auto renamed = std::make_shared<DataNs::TileClass>(left.getId(), temporaryDirectory.path / "image.tif",
                                                             left.getTiff(), SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT, CV_16UC3);
    dataStore.replace("tiles", DataNs::TileList{renamed, tiles[1]});
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult().value("error", "").find("_rLL_cCC"), std::string::npos);
}

// Les coins doivent être ceux de graphcut, au pixel près, y compris pour des positions fractionnaires et négatives.
TEST(FusionStepTest, ComputesTheSameCornersAsGraphCut) {
    const TemporaryDirectoryClass temporaryDirectory;
    const std::vector<cv::Point2d> positions{{-3.5, 2.5}, {10.2, -7.7}, {0.5, 0.49}, {-2.5, -7.5}};
    std::vector<DataNs::TilePositionData> namedPositions;
    std::vector<std::string> tilePaths;
    for (std::size_t tileIndex = 0; tileIndex < positions.size(); ++tileIndex) {
        const int column = static_cast<int>(tileIndex);
        tilePaths.push_back((temporaryDirectory.path / (tileStem(column) + ".tif")).string());
        cv::imwrite(tilePaths.back(), cv::Mat(SMALL_TILE_HEIGHT, SMALL_TILE_WIDTH, CV_8UC3, cv::Scalar::all(1)));
        namedPositions.push_back({"r00_c0" + std::to_string(column), positions[tileIndex], 0.0});
    }
    std::vector<cv::Size> tileSizes;
    std::vector<cv::UMat> smallMasks;
    std::vector<cv::Point> graphCutCorners;
    std::vector<cv::Point> smallCorners;
    ASSERT_EQ(GraphCutNs::ImagePretreatmentClass().processImages(namedPositions, tilePaths, 1.0, tileSizes, smallMasks,
                                                                 graphCutCorners, smallCorners),
              1);

    EXPECT_EQ(StepsNs::computeMosaicCorners(positions), graphCutCorners);
}

TEST(FusionStepTest, MosaicIsPublishedByExport) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    StepsNs::FusionStepClass fusion = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);
    StepsNs::ExportStepClass exportStep(StepsNs::ExportInput{(temporaryDirectory.path / "output").u8string()});
    exportStep.setDataStore(dataStore);

    ASSERT_TRUE(fusion.execute()) << fusion.getResult().dump();
    ASSERT_TRUE(exportStep.execute()) << exportStep.getResult().dump();

    const DataNs::Mosaic& mosaic = dataStore.get<DataNs::Mosaic>("mosaic");
    EXPECT_EQ(exportStep.getOutput().canvasSize, mosaic.canvasSize);
    EXPECT_TRUE(areEqual(readImage(exportStep.getOutput().imagePath, mosaic.canvasSize),
                         readImage(mosaic.imagePath, mosaic.canvasSize)));
}

// Un fichier produit qui disparaît change l'empreinte : le checkpoint de fusion est alors périmé.
TEST(FusionStepTest, FingerprintFollowsTheProducedFiles) {
    const TemporaryDirectoryClass temporaryDirectory;
    StepNs::DataStoreClass dataStore;
    fillTwoTileStore(dataStore, temporaryDirectory.path / "tiles", smallScene());
    StepsNs::FusionStepClass step = makeFusionStep(temporaryDirectory.path / "fusion", dataStore);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const Json fingerprint = step.externalInputsFingerprint();

    fs::remove(dataStore.get<DataNs::Mosaic>("mosaic").maskPath);

    EXPECT_NE(step.externalInputsFingerprint(), fingerprint);
}

// Chaînage : fusion a besoin des territoires de graphcut, et export de la mosaïque de fusion.
TEST(FusionStepTest, IsChainedBetweenGraphCutAndExport) {
    const Json chained = {{"pipeline", {{"steps", {{{"name", "import"}}, {{"name", "phase-correlation"}},
                                                   {{"name", "absolute-pos"}}, {{"name", "graphcut"}},
                                                   {{"name", "fusion"}}, {{"name", "export"}}}}}}};
    const Json withoutGraphCut = {{"pipeline", {{"steps", {{{"name", "import"}}, {{"name", "phase-correlation"}},
                                                           {{"name", "absolute-pos"}}, {{"name", "fusion"}}}}}}};

    EXPECT_NO_THROW(PipelineNs::buildPipeline(chained));
    EXPECT_THROW(PipelineNs::buildPipeline(withoutGraphCut), std::invalid_argument);
}
