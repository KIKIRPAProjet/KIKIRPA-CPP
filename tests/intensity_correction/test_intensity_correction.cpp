// tests/intensity_correction/test_intensity_correction.cpp
// Step intensity-correction : paramètres, échecs et estimation d'un gain connu sur de vraies tuiles 16 bits.
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"
#include "steps/intensity_correction_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {

    using Json = nlohmann::json;
    namespace fs = std::filesystem;

    constexpr int TILE_WIDTH = 400;           // px
    constexpr int TILE_HEIGHT = 300;          // px
    constexpr int TILE_OFFSET_X = 200;        // px : coin de B dans A (recouvrement de 50 %)
    constexpr double OVERLAP_FRACTION = 0.5;  // recouvrement nominal donné à la corrélation de phase
    constexpr double TEXTURE_BLUR_SIGMA = 2.0;
    constexpr double TEXTURE_MIN = 2000.0;    // valeurs 16 bits : la plage laisse de la marge pour le gain sans saturer
    constexpr double TEXTURE_MAX = 40000.0;
    constexpr double TILE_B_GAIN = 1.25;      // B est 25 % plus lumineuse que A
    constexpr double GAIN_TOLERANCE = 0.02;   // erreur relative acceptée sur le rapport des gains

    // Deux tuiles 16 bits img_C00_R00.tif et img_C01_R00.tif découpées dans la même texture : dans le recouvrement,
    // B vaut exactement TILE_B_GAIN fois A.
    void createTiles(const fs::path& directory) {
        fs::remove_all(directory);
        fs::create_directories(directory);
        cv::Mat texture(TILE_HEIGHT, TILE_OFFSET_X + TILE_WIDTH, CV_32F);
        cv::RNG rng(12345);
        rng.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(texture, texture, cv::Size(), TEXTURE_BLUR_SIGMA);
        cv::normalize(texture, texture, TEXTURE_MIN, TEXTURE_MAX, cv::NORM_MINMAX);

        cv::Mat tileA, tileB;
        texture(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT)).convertTo(tileA, CV_16U);
        texture(cv::Rect(TILE_OFFSET_X, 0, TILE_WIDTH, TILE_HEIGHT)).convertTo(tileB, CV_16U, TILE_B_GAIN);
        cv::imwrite((directory / "img_C00_R00.tif").string(), tileA);
        cv::imwrite((directory / "img_C01_R00.tif").string(), tileB);
    }

    Json parameter(const std::string& valueType, const std::string& name, const Json& value) {
        return { {"valueType", valueType}, {"name", name}, {"value", value} };
    }

    // Script import -> phase-correlation -> intensity-correction. std::vector et non Json : avec nlohmann,
    // {x} à un seul élément ne donne pas un tableau.
    Json buildScript(const fs::path& directory, const std::string& method) {
        const std::vector<Json> steps = {
            {{"name", "import"}, {"parameters", std::vector<Json>{parameter("string", "path", directory.string())}}},
            {{"name", "phase-correlation"}, {"parameters", std::vector<Json>{parameter("float", "overlap", OVERLAP_FRACTION)}}},
            {{"name", "intensity-correction"}, {"parameters", std::vector<Json>{parameter("string", "method", method)}}} };
        return { {"pipeline", {{"steps", steps}}} };
    }

    // Exécute le pipeline réel sur des tuiles fraîches et renvoie la correction estimée.
    // Si le pipeline échoue, son journal est affiché et la correction renvoyée est vide.
    DataNs::IntensityCorrection runPipeline(const std::string& method) {
        const fs::path directory = fs::temp_directory_path() / "kikirpa_test_intensity_correction";
        createTiles(directory);
        PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(buildScript(directory, method));
        std::string journal;
        const bool succeeded = pipeline.run([&](const std::string& message) { journal += message + "\n"; });
        EXPECT_TRUE(succeeded) << journal;
        DataNs::IntensityCorrection correction;
        if (succeeded) correction = pipeline.getDataStore().get<DataNs::IntensityCorrection>("intensityCorrection");
        fs::remove_all(directory);
        return correction;
    }

    // Configure un step seul avec les paramètres donnés, puis l'exécute sur le DataStoreClass donné.
    bool executeStep(StepsNs::IntensityCorrectionStepClass& step, StepNs::DataStoreClass& dataStore,
        const std::vector<Json>& parameters = {}) {
        step.importConfig({ {"name", "intensity-correction"}, {"parameters", parameters} });
        step.setDataStore(dataStore);
        return step.execute();
    }

    std::string errorOf(const StepsNs::IntensityCorrectionStepClass& step) {
        return step.getResult().value("error", "");
    }

}

TEST(IntensityCorrectionStepTest, IsRegisteredUnderItsName) {
    const auto step = PipelineNs::createStep("intensity-correction");
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->getConfigJson().at("name"), "intensity-correction");
}

TEST(IntensityCorrectionStepTest, RecoversKnownTileGain) {
    const DataNs::IntensityCorrection correction = runPipeline("gain");

    ASSERT_EQ(correction.tileGains.size(), 2u);
    // Le gain d'une tuile est sa luminosité relative (la correction divise par le gain) : B étant TILE_B_GAIN fois
    // plus lumineuse que A, gainB / gainA = TILE_B_GAIN.
    const double estimatedRatio = correction.tileGains[1] / correction.tileGains[0];
    EXPECT_NEAR(estimatedRatio / TILE_B_GAIN, 1.0, GAIN_TOLERANCE);
    EXPECT_DOUBLE_EQ(correction.fieldStrength, 0.0);   // la méthode "gain" n'estime pas de champ d'éclairage
    EXPECT_TRUE(correction.isolatedTileIndices.empty());
}

TEST(IntensityCorrectionStepTest, KeepsGainsCenteredOnOne) {
    const DataNs::IntensityCorrection correction = runPipeline("gain-and-field");

    ASSERT_EQ(correction.tileGains.size(), 2u);
    const double logSum = std::log(correction.tileGains[0]) + std::log(correction.tileGains[1]);
    EXPECT_NEAR(logSum, 0.0, 1e-6);   // la moyenne des logarithmes des gains est nulle
}

TEST(IntensityCorrectionStepTest, RejectsUnknownMethod) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});
    dataStore.add("matches", std::vector<DataNs::PairMatches>{});

    EXPECT_FALSE(executeStep(step, dataStore, { parameter("string", "method", "foo") }));
    EXPECT_NE(errorOf(step).find("foo"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, FailsWithoutMatches) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});

    EXPECT_FALSE(executeStep(step, dataStore));
    EXPECT_NE(errorOf(step).find("matches"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, FailsOnUnknownTileId) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});   // aucune tuile : tout identifiant des matches est inconnu
    dataStore.add("matches", std::vector<DataNs::PairMatches>{{7, 8, DataNs::Direction::Right, {}}});

    EXPECT_FALSE(executeStep(step, dataStore));
    EXPECT_NE(errorOf(step).find("unknown tile id 7"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, RejectsInvalidAlgorithmParameter) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});
    dataStore.add("matches", std::vector<DataNs::PairMatches>{});

    EXPECT_FALSE(executeStep(step, dataStore, { parameter("int", "iterationCount", 0) }));
    EXPECT_NE(errorOf(step).find("invalid parameters"), std::string::npos);
}