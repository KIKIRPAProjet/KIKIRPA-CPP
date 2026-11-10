// tests/intensity_correction/test_intensity_correction.cpp
// Step intensity-correction : paramètres, échecs et estimation d'un gain connu sur de vraies tuiles 16 bits.
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/checkpoint/checkpoint.h"
#include "core/pipeline/pipeline.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"
#include "steps/intensity_correction_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/hal/interface.h>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
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
    constexpr double LOG_GAIN_TOLERANCE = 1e-6;
    constexpr int TEXTURE_SEED = 12345;
    constexpr int UNKNOWN_TILE_ID = 7;
    constexpr double GAIN_TOLERANCE = 0.02;   // erreur relative acceptée sur le rapport des gains

    struct TestDirectory {
        fs::path path;
        TestDirectory() {
            auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("intensity-correction-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~TestDirectory() { std::error_code error; fs::remove_all(path, error); }
    };

    DataNs::TileList makeTiles(const fs::path& directory) {
        return {std::make_shared<DataNs::TileClass>(0, directory / "img_C00_R00.tif", "", TILE_WIDTH, TILE_HEIGHT, CV_16UC1),
                std::make_shared<DataNs::TileClass>(1, directory / "img_C01_R00.tif", "", TILE_WIDTH, TILE_HEIGHT, CV_16UC1)};
    }

    DataNs::PairMatches makePair() {
        DataNs::PairMatches pair;
        pair.tileIdA = 0;
        pair.tileIdB = 1;
        pair.featureMatches.translationX = TILE_OFFSET_X;
        return pair;
    }

    void fillStore(StepNs::DataStoreClass& store, const fs::path& directory) {
        store.replace("tiles", makeTiles(directory));
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair()});
    }

    // Deux tuiles 16 bits img_C00_R00.tif et img_C01_R00.tif découpées dans la même texture : dans le recouvrement,
    // B vaut exactement TILE_B_GAIN fois A.
    void createTiles(const fs::path& directory) {
        fs::remove_all(directory);
        fs::create_directories(directory);
        cv::Mat texture(TILE_HEIGHT, TILE_OFFSET_X + TILE_WIDTH, CV_32F);
        cv::RNG rng(TEXTURE_SEED);
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
        TestDirectory scope;
        const fs::path directory = scope.path;
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
    EXPECT_NEAR(logSum, 0.0, LOG_GAIN_TOLERANCE);   // la moyenne des logarithmes des gains est nulle
}

TEST(IntensityCorrectionStepTest, RejectsUnknownMethod) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});
    dataStore.add("matches", std::vector<DataNs::PairMatches>{});

    EXPECT_FALSE(executeStep(step, dataStore, { parameter("string", "method", "foo") }));
    EXPECT_NE(errorOf(step).find("method"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, FailsWithoutMatches) {
    TestDirectory directory;
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", makeTiles(directory.path));

    EXPECT_FALSE(executeStep(step, dataStore));
    EXPECT_NE(errorOf(step).find("matches"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, FailsWhenAllPairIdsAreUnknown) {
    TestDirectory directory;
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    auto pair = makePair();
    pair.tileIdA = UNKNOWN_TILE_ID;
    store.replace("matches", std::vector<DataNs::PairMatches>{pair});
    EXPECT_FALSE(executeStep(step, store));
    ASSERT_EQ(step.getResult()["failures"].size(), 1U);
    EXPECT_EQ(step.getResult()["failures"][0]["reason"], "unknown tile id");
}

TEST(IntensityCorrectionStepTest, RejectsInvalidAlgorithmParameter) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", DataNs::TileList{});
    dataStore.add("matches", std::vector<DataNs::PairMatches>{});

    EXPECT_FALSE(executeStep(step, dataStore, { parameter("int", "iterationCount", 0) }));
    EXPECT_FALSE(errorOf(step).empty());
}
TEST(IntensityCorrectionStepTest, ClearsPreviousOutputAfterFailure) {
    TestDirectory directory;
    createTiles(directory.path);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store));
    ASSERT_FALSE(step.getOutput().value.tileGains.empty());
    store.replace("matches", std::vector<DataNs::PairMatches>{});
    EXPECT_FALSE(step.execute());
    EXPECT_TRUE(step.getOutput().value.tileGains.empty());
    EXPECT_TRUE(store.get<DataNs::IntensityCorrection>("intensityCorrection").tileGains.empty());
}

TEST(IntensityCorrectionStepTest, IgnoresInvalidPairsWhenAnotherPairSucceeds) {
    TestDirectory directory;
    createTiles(directory.path);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    auto unknown = makePair(), self = makePair(), nonFinite = makePair();
    unknown.tileIdB = UNKNOWN_TILE_ID;
    self.tileIdB = 0;
    nonFinite.featureMatches.translationY = std::numeric_limits<float>::quiet_NaN();
    store.replace("matches", std::vector<DataNs::PairMatches>{unknown, self, nonFinite, makePair(), makePair()});
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store)) << step.getResult();
    EXPECT_EQ(step.getResult()["pairs"], 1);
    EXPECT_EQ(step.getResult()["failures"].size(), 4U);
    EXPECT_EQ(step.getOutput().key, "intensityCorrection");
    EXPECT_EQ(step.getOutput().type, "IntensityCorrection");
    EXPECT_FALSE(step.getResult().contains("tileGains"));
}

TEST(IntensityCorrectionStepTest, RejectsNullDuplicateAndSingleTiles) {
    TestDirectory directory;
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    StepsNs::IntensityCorrectionStepClass step;
    store.replace("tiles", DataNs::TileList{nullptr, nullptr});
    EXPECT_FALSE(executeStep(step, store));
    auto tiles = makeTiles(directory.path);
    tiles[1] = tiles[0];
    store.replace("tiles", tiles);
    EXPECT_FALSE(step.execute());
    tiles.resize(1);
    store.replace("tiles", tiles);
    EXPECT_FALSE(step.execute());
}

TEST(IntensityCorrectionStepTest, CatchesWrongDataStoreTypesAndMissingImages) {
    TestDirectory directory;
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    StepsNs::IntensityCorrectionStepClass step;
    EXPECT_FALSE(executeStep(step, store));
    EXPECT_NE(errorOf(step).find("cannot read"), std::string::npos);
    store.replace("tiles", std::string("wrong type"));
    EXPECT_NO_THROW(EXPECT_FALSE(step.execute()));
}

TEST(IntensityCorrectionStepTest, ValidatesDefaultsTypesAndIntegerBounds) {
    StepsNs::IntensityCorrectionStepClass step;
    StepNs::DataStoreClass store;
    EXPECT_FALSE(step.execute());
    step.importConfig({{"name", "intensity-correction"}});
    EXPECT_EQ(step.getConfigJson()["parameters"].size(), 8U);
    EXPECT_THROW(step.importConfig({{"parameters", {parameter("float", "unknown", 1.0)}}}), std::invalid_argument);
    EXPECT_THROW(step.importConfig({{"parameters", {parameter("int", "iterationCount", "wrong")}}}), std::invalid_argument);
    EXPECT_FALSE(executeStep(step, store, {parameter("int", "iterationCount", std::numeric_limits<unsigned long long>::max())}));
    EXPECT_NE(errorOf(step).find("range"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, RejectsNonFiniteParametersFromCppInput) {
    StepsNs::IntensityCorrectionInput input;
    input.algorithmParameters.blurSigma = std::numeric_limits<double>::infinity();
    StepsNs::IntensityCorrectionStepClass step(input);
    StepNs::DataStoreClass store;
    EXPECT_FALSE(executeStep(step, store));
    EXPECT_NE(errorOf(step).find("finite"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, RestoresCorrectionFromCheckpoint) {
    TestDirectory directory;
    createTiles(directory.path);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store));
    CheckpointNs::CheckpointClass checkpoint(directory.path / "checkpoints");
    std::string reason;
    ASSERT_TRUE(checkpoint.save(0, step, store, reason)) << reason;
    ASSERT_TRUE(checkpoint.valid(0, step));
    StepNs::DataStoreClass restored;
    checkpoint.load(0, step, "intensityCorrection", "IntensityCorrection", restored);
    auto correction = restored.get<DataNs::IntensityCorrection>("intensityCorrection");
    EXPECT_EQ(correction.tileGains, step.getOutput().value.tileGains);
    EXPECT_EQ(correction.fieldStrength, step.getOutput().value.fieldStrength);
    EXPECT_EQ(correction.isolatedTileIndices, step.getOutput().value.isolatedTileIndices);
}

TEST(IntensityCorrectionStepTest, ReportsEmptyOverlapInsteadOfPublishingCorrection) {
    TestDirectory directory;
    createTiles(directory.path);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    auto pair = makePair();
    pair.featureMatches.translationX = 2 * TILE_WIDTH;
    store.replace("matches", std::vector<DataNs::PairMatches>{pair});
    StepsNs::IntensityCorrectionStepClass step;
    EXPECT_FALSE(executeStep(step, store));
    ASSERT_EQ(step.getResult()["failures"].size(), 1U);
    EXPECT_EQ(step.getResult()["failures"][0]["reason"], "no valid overlap samples");
    EXPECT_TRUE(store.get<DataNs::IntensityCorrection>("intensityCorrection").tileGains.empty());
}

TEST(IntensityCorrectionStepTest, LeavesIsolatedTileWithUnitGain) {
    TestDirectory directory;
    createTiles(directory.path);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    auto tiles = makeTiles(directory.path);
    auto isolatedPath = directory.path / "img_C02_R00.tif";
    fs::copy_file(tiles[0]->getSource(), isolatedPath);
    tiles.push_back(std::make_shared<DataNs::TileClass>(2, isolatedPath, "", TILE_WIDTH, TILE_HEIGHT, CV_16UC1));
    store.replace("tiles", tiles);
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store)) << step.getResult();
    ASSERT_EQ(step.getOutput().value.tileGains.size(), 3U);
    EXPECT_NEAR(step.getOutput().value.tileGains[2], 1.0, LOG_GAIN_TOLERANCE);
    EXPECT_EQ(step.getOutput().value.isolatedTileIndices, std::vector<int>{2});
}

TEST(IntensityCorrectionStepTest, RejectsMetadataMismatchAndPreservesSourceFiles) {
    TestDirectory directory;
    createTiles(directory.path);
    auto path = directory.path / "img_C00_R00.tif";
    auto before = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store));
    auto after = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
    EXPECT_EQ(cv::norm(before, after, cv::NORM_INF), 0.0);
    auto tiles = makeTiles(directory.path);
    tiles[0] = std::make_shared<DataNs::TileClass>(0, path, "", TILE_WIDTH + 1, TILE_HEIGHT, CV_16UC1);
    store.replace("tiles", tiles);
    EXPECT_FALSE(step.execute());
    EXPECT_NE(errorOf(step).find("metadata"), std::string::npos);
}

TEST(IntensityCorrectionStepTest, SupportsEightBitInputWithoutChangingGainConvention) {
    constexpr double DEPTH_SCALE = 1.0 / 257.0;
    TestDirectory directory;
    createTiles(directory.path);
    auto tiles = makeTiles(directory.path);
    for (auto& tile : tiles) {
        auto pixels = cv::imread(tile->getSource().string(), cv::IMREAD_UNCHANGED);
        pixels.convertTo(pixels, CV_8U, DEPTH_SCALE);
        ASSERT_TRUE(cv::imwrite(tile->getSource().string(), pixels));
        tile = std::make_shared<DataNs::TileClass>(tile->getId(), tile->getSource(), "", TILE_WIDTH, TILE_HEIGHT, CV_8UC1);
    }
    StepNs::DataStoreClass store;
    fillStore(store, directory.path);
    store.replace("tiles", tiles);
    StepsNs::IntensityCorrectionStepClass step;
    ASSERT_TRUE(executeStep(step, store, {parameter("string", "method", "gain")})) << step.getResult();
    auto gains = step.getOutput().value.tileGains;
    EXPECT_NEAR(gains[1] / gains[0], TILE_B_GAIN, GAIN_TOLERANCE);
}
