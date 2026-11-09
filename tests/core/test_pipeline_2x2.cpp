#include <gtest/gtest.h>

#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/pipeline.h"
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/data/tile.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

class TempWorkspace {
public:
    TempWorkspace() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("kikirpa_test_2x2_" + std::to_string(now));
        imagesDir = root / "images";
        outputDir = root / "output";
        fs::create_directories(imagesDir);
        fs::create_directories(outputDir);
    }

    ~TempWorkspace() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path root;
    fs::path imagesDir;
    fs::path outputDir;
};

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int OVERLAP_X = 80;   // 20% de 400
constexpr int OVERLAP_Y = 60;   // 20% de 300
constexpr int SHIFT_X = TILE_WIDTH - OVERLAP_X;    // 320 px
constexpr int SHIFT_Y = TILE_HEIGHT - OVERLAP_Y;  // 240 px
constexpr int CANVAS_WIDTH = SHIFT_X + TILE_WIDTH;   // 720 px
constexpr int CANVAS_HEIGHT = SHIFT_Y + TILE_HEIGHT; // 540 px

// Génère une grande scène texturée synthétique et découpe 4 tuiles avec recouvrement exact
void createSyntheticTiles2x2(const fs::path& imagesDir) {
    cv::Mat canvas32f(CANVAS_HEIGHT, CANVAS_WIDTH, CV_32FC3);
    cv::RNG rng(4242);
    rng.fill(canvas32f, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(canvas32f, canvas32f, cv::Size(), 2.0);
    cv::normalize(canvas32f, canvas32f, 2000.0, 60000.0, cv::NORM_MINMAX);

    cv::Mat canvas16u;
    canvas32f.convertTo(canvas16u, CV_16UC3);

    // Tuile (0, 0)
    cv::Mat t00 = canvas16u(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c00.tif").string(), t00);

    // Tuile (0, 1) - décalée à droite de SHIFT_X
    cv::Mat t01 = canvas16u(cv::Rect(SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c01.tif").string(), t01);

    // Tuile (1, 0) - décalée vers le bas de SHIFT_Y
    cv::Mat t10 = canvas16u(cv::Rect(0, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c00.tif").string(), t10);

    // Tuile (1, 1) - décalée de SHIFT_X et SHIFT_Y
    cv::Mat t11 = canvas16u(cv::Rect(SHIFT_X, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c01.tif").string(), t11);
}

Json build2x2PipelineScript(const fs::path& imagesDir, const fs::path& outputDir) {
    return {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", imagesDir.string()}},
                        {{"valueType", "string"}, {"name", "format"}, {"value", ".*_r(?=\\d+_c(\\d+)\\.tiff?$)(\\d+)_c\\d+\\.tiff?"}}
                    })}
                },
                {
                    {"name", "phase-correlation"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "niveau"}, {"value", 0}},
                        {{"valueType", "float"}, {"name", "overlap"}, {"value", 0.2f}}
                    })}
                },
                {
                    {"name", "absolute-pos"}
                },
                {
                    {"name", "intensity-correction"}
                },
                {
                    {"name", "graphcut"},
                    {"parameters", Json::array({
                        {{"valueType", "float"}, {"name", "scale"}, {"value", 0.5f}}
                    })}
                },
                {
                    {"name", "fusion"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "blockSize"}, {"value", 512}}
                    })}
                },
                {
                    {"name", "export"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "outputDirectory"}, {"value", outputDir.string()}}
                    })}
                }
            })}
        }}
    };
}

} // namespace

// Rejet précis des configurations invalides
TEST(Pipeline2x2Test, RejectsInvalidConfigurationWithSpecificCause) {
    // 1. Step manquant ou inconnu
    const Json scriptInconnu = {
        {"pipeline", {
            {"steps", Json::array({
                {{"name", "step_inexistant"}}
            })}
        }}
    };
    EXPECT_THROW({
        try {
            PipelineNs::buildPipeline(scriptInconnu);
        } catch (const std::invalid_argument& ex) {
            EXPECT_NE(std::string(ex.what()).find("unknown step"), std::string::npos);
            throw;
        }
    }, std::invalid_argument);

    // 2. Rupture d'enchaînement : graphcut avant que 'positions' ne soit produit
    const Json scriptChaineInvalide = {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", "dummy/"}}
                    })}
                },
                {{"name", "graphcut"}} // Erreur : réclame 'positions' que personne n'a produit
            })}
        }}
    };
    EXPECT_THROW({
        try {
            PipelineNs::buildPipeline(scriptChaineInvalide);
        } catch (const std::invalid_argument& ex) {
            EXPECT_NE(std::string(ex.what()).find("positions"), std::string::npos);
            throw;
        }
    }, std::invalid_argument);
}

// Exécution complète et réelle sur un jeu 2×2 jusqu'à l'export sans transfert manuel
TEST(Pipeline2x2Test, ExecutesFullChainOnReal2x2DatasetUntilExport) {
    TempWorkspace ws;
    createSyntheticTiles2x2(ws.imagesDir);

    const Json script = build2x2PipelineScript(ws.imagesDir, ws.outputDir);

    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(script);

    std::vector<std::string> logs;
    const bool success = pipeline.run([&logs](const std::string& msg) {
        logs.push_back(msg);
        std::cout << "    [pipeline 2x2] " << msg << std::endl;
    });

    EXPECT_TRUE(success);

    const auto& store = pipeline.getDataStore();

    // 1. Import
    ASSERT_TRUE(store.has("tiles"));
    const auto& tiles = store.get<DataNs::TileList>("tiles");
    EXPECT_EQ(tiles.size(), 4u);

    // 2. Appariement
    ASSERT_TRUE(store.has("matches"));
    const auto& matches = store.get<std::vector<DataNs::PairMatches>>("matches");
    EXPECT_EQ(matches.size(), 4u); // 2 paires horizontales, 2 paires verticales

    // 3. Positionnement absolu
    ASSERT_TRUE(store.has("positions"));
    const auto& positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
    EXPECT_EQ(positions.size(), 4u);

    // 4. Correction d'intensité
    ASSERT_TRUE(store.has("intensityCorrection"));
    const auto& ic = store.get<DataNs::IntensityCorrection>("intensityCorrection");
    EXPECT_EQ(ic.tileGains.size(), 4u);

    // 5. Découpe des coutures (graphcut)
    ASSERT_TRUE(store.has("territories"));
    const auto& territories = store.get<std::vector<DataNs::TileTerritory>>("territories");
    EXPECT_EQ(territories.size(), 4u);

    // 6. Assemblage / Fusion
    ASSERT_TRUE(store.has("compositeBlocks"));
    const auto& blocks = store.get<DataNs::CompositeBlockList>("compositeBlocks");
    EXPECT_FALSE(blocks.empty());

    // 7. Export sur disque
    const fs::path expectedMosaicPath = ws.outputDir / "mosaic.tif";
    EXPECT_TRUE(fs::exists(expectedMosaicPath));
    EXPECT_GT(fs::file_size(expectedMosaicPath), 0u);

    // Vérification de l'intégrité de l'image finale exportée
    cv::Mat exportedImage = cv::imread(expectedMosaicPath.string(), cv::IMREAD_UNCHANGED);
    ASSERT_FALSE(exportedImage.empty());
    EXPECT_EQ(exportedImage.type(), CV_16UC3);
    EXPECT_GE(exportedImage.cols, CANVAS_WIDTH - 20);
    EXPECT_GE(exportedImage.rows, CANVAS_HEIGHT - 20);
}
