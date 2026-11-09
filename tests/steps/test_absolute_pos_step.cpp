// tests/steps/test_absolute_pos_step.cpp
#include "steps/absolute_pos_step.h"

#include "algorithms/stitching/positioning/position_solver.h"
#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/checkpoint/checkpoint.h"
#include "core/pipeline/pipeline.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    constexpr int TILE_WIDTH = 100;
    constexpr int TILE_HEIGHT = 80;
    constexpr double TOLERANCE = 1e-8;
    constexpr float SHIFT = 10.0f;
    constexpr float INCONSISTENT_SHIFT = 23.0f;
    constexpr double ADJUSTED_FIRST = 11.0;
    constexpr double ADJUSTED_SECOND = 22.0;
    constexpr double ROTATION = 0.125;

    DataNs::TileList makeTiles(int count = 3) {
        DataNs::TileList tiles;
        for (int index = 0; index < count; ++index) {
            auto path = "tile_r01_c0" + std::to_string(index + 1) + ".tif";
            tiles.push_back(std::make_shared<DataNs::TileClass>(index, path, "cache.tif", TILE_WIDTH, TILE_HEIGHT, CV_8UC1));
        }
        return tiles;
    }

    DataNs::PairMatches makePair(int first, int second, cv::Point2f translation = {SHIFT, 0.0f}) {
        DataNs::PairMatches pair;
        pair.tileIdA = first;
        pair.tileIdB = second;
        pair.featureMatches.translationX = translation.x;
        pair.featureMatches.translationY = translation.y;
        return pair;
    }

    void fillStore(StepNs::DataStoreClass& store) {
        store.replace("tiles", makeTiles());
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair(0, 1), makePair(1, 2)});
    }

    struct TestDirectory {
        std::filesystem::path path;
        TestDirectory() {
            auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("absolute-pos-test-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    TEST(AbsolutePosStep, PlacesAllTilesAndExportsOnlySummary) {
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        auto positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
        ASSERT_EQ(positions.size(), 3U);
        EXPECT_NEAR(positions[0].position.x, 0.0, TOLERANCE);
        EXPECT_NEAR(positions[1].position.x, SHIFT, TOLERANCE);
        EXPECT_NEAR(positions[2].position.x, 2 * SHIFT, TOLERANCE);
        EXPECT_EQ(positions[1].tileId, "r01_c02");
        EXPECT_EQ(step.getOutput().key, "positions");
        EXPECT_EQ(step.getOutput().type, "Positions");
        EXPECT_EQ(step.getResult()["positions"], 3);
        EXPECT_FALSE(step.getResult().contains("value"));
    }

    TEST(AbsolutePosStep, AdjustsInconsistentCycleInsteadOfChoosingOnePath) {
        StepNs::DataStoreClass store;
        fillStore(store);
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair(0, 1), makePair(1, 2), makePair(0, 2, {INCONSISTENT_SHIFT, 0.0f})});
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        EXPECT_NEAR(step.getOutput().value[1].position.x, ADJUSTED_FIRST, TOLERANCE);
        EXPECT_NEAR(step.getOutput().value[2].position.x, ADJUSTED_SECOND, TOLERANCE);
    }

    TEST(AbsolutePosStep, UsesConfiguredReferenceAndSignedVerticalTranslations) {
        StepNs::DataStoreClass store;
        fillStore(store);
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair(0, 1, {0.0f, SHIFT}), makePair(1, 2, {0.0f, -SHIFT})});
        StepsNs::AbsolutePosStepClass step;
        step.importConfig({{"parameters", {{{"valueType", "int"}, {"name", "referenceTileId"}, {"value", 1}}}}});
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        EXPECT_NEAR(step.getOutput().value[0].position.y, -SHIFT, TOLERANCE);
        EXPECT_NEAR(step.getOutput().value[1].position.y, 0.0, TOLERANCE);
        EXPECT_NEAR(step.getOutput().value[2].position.y, -SHIFT, TOLERANCE);
    }

    TEST(AbsolutePosStep, IgnoresInvalidPairsWhenRemainingGraphIsConnected) {
        StepNs::DataStoreClass store;
        fillStore(store);
        auto pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
        pairs.push_back(makePair(0, 3));
        pairs.push_back(makePair(0, 0));
        pairs.push_back(makePair(0, 2, {std::numeric_limits<float>::quiet_NaN(), 0.0f}));
        store.replace("matches", pairs);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        EXPECT_EQ(step.getResult()["failures"].size(), 3U);
        EXPECT_EQ(step.getResult()["pairs"], 2);
        EXPECT_EQ(step.getResult()["failures"][0]["idB"], 3);
    }

    TEST(AbsolutePosStep, RejectsDisconnectedGraphAndClearsPreviousOutput) {
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute());
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair(0, 1)});
        EXPECT_FALSE(step.execute());
        EXPECT_TRUE(step.getOutput().value.empty());
        EXPECT_TRUE(store.get<std::vector<DataNs::TilePositionData>>("positions").empty());
        EXPECT_TRUE(store.get<std::vector<DataNs::PairResidual>>("pairResiduals").empty());
        EXPECT_FALSE(step.getResult()["error"].get<std::string>().empty());
    }

    TEST(AbsolutePosStep, PlacesSingleTileWithoutMeasurements) {
        StepNs::DataStoreClass store;
        store.replace("tiles", makeTiles(1));
        store.replace("matches", std::vector<DataNs::PairMatches>{});
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        ASSERT_EQ(step.getOutput().value.size(), 1U);
        EXPECT_EQ(step.getOutput().value[0].position, cv::Point2d(0.0, 0.0));
    }

    TEST(AbsolutePosStep, RejectsMissingInputsAndInvalidTileLists) {
        StepsNs::AbsolutePosStepClass step;
        EXPECT_FALSE(step.execute());
        StepNs::DataStoreClass store;
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
        fillStore(store);
        store.replace("tiles", DataNs::TileList{});
        EXPECT_FALSE(step.execute());
        store.replace("tiles", DataNs::TileList{nullptr});
        EXPECT_FALSE(step.execute());
        auto tiles = makeTiles();
        tiles[1] = tiles[0];
        store.replace("tiles", tiles);
        EXPECT_FALSE(step.execute());
    }

    TEST(AbsolutePosStep, RejectsUnknownReferenceAndInvalidParameterType) {
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step({3});
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
        EXPECT_THROW(step.importConfig({{"parameters", {{{"valueType", "int"}, {"name", "referenceTileId"}, {"value", "wrong"}}}}}), std::invalid_argument);
    }

    TEST(AbsolutePosStep, IsAvailableFromJsonRegistry) {
        auto step = PipelineNs::createStep("absolute-pos");
        EXPECT_EQ(step->getConfigJson()["outputs"][0]["dataType"], "Positions");
        EXPECT_EQ(step->getConfigJson()["inputs"][1]["dataType"], "PairMatches");
        step->importConfig({{"name", "absolute-pos"}, {"parameters", nlohmann::json::array()}});
    }

    TEST(PositionSolver, PreservesLegacyRotationAndDisconnectedComponentContract) {
        std::vector<PositioningNs::RelativeTilePosition> offsets = {{0, 1, {SHIFT, -SHIFT}, ROTATION}};
        auto solution = PositioningNs::solveRelativePositions(3, offsets);
        EXPECT_TRUE(solution.connected[1]);
        EXPECT_FALSE(solution.connected[2]);
        EXPECT_NEAR(solution.positions[1].x, SHIFT, TOLERANCE);
        EXPECT_NEAR(solution.rotations[1], ROTATION, TOLERANCE);
        EXPECT_THROW(PositioningNs::solveRelativePositions(0, {}), std::invalid_argument);
        EXPECT_THROW(PositioningNs::solveRelativePositions(1, offsets), std::invalid_argument);
    }

    TEST(AbsolutePosStep, RestoresPositionsFromCheckpoint) {
        TestDirectory directory;
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute());
        CheckpointNs::CheckpointClass checkpoint(directory.path);
        std::string failureReason;
        ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
        ASSERT_TRUE(checkpoint.valid(0, step));
        StepNs::DataStoreClass restored;
        checkpoint.load(0, step, "positions", "Positions", restored);
        auto positions = restored.get<std::vector<DataNs::TilePositionData>>("positions");
        ASSERT_EQ(positions.size(), 3U);
        EXPECT_EQ(positions[2].tileId, "r01_c03");
        EXPECT_NEAR(positions[2].position.x, 2 * SHIFT, TOLERANCE);
    }

    TEST(AbsolutePosStep, RejectsMalformedNamesAndDuplicateGridCoordinates) {
        StepNs::DataStoreClass store;
        fillStore(store);
        auto tiles = makeTiles();
        tiles[1] = std::make_shared<DataNs::TileClass>(1, "bad.tif", "cache.tif", TILE_WIDTH, TILE_HEIGHT, CV_8UC1);
        store.replace("tiles", tiles);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
        tiles[1] = std::make_shared<DataNs::TileClass>(1, "tile_r01_c01.tif", "cache.tif", TILE_WIDTH, TILE_HEIGHT, CV_8UC1);
        store.replace("tiles", tiles);
        EXPECT_FALSE(step.execute());
    }

    TEST(AbsolutePosStep, RejectsOutOfRangeIntegerWithoutWrappingToDefaultReference) {
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step;
        step.importConfig({{"parameters", {{{"valueType", "int"}, {"name", "referenceTileId"},
                                            {"value", std::numeric_limits<unsigned long long>::max()}}}}});
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
    }

    TEST(AbsolutePosStep, DefaultReferenceUsesSmallestIdRatherThanInputOrder) {
        StepNs::DataStoreClass store;
        fillStore(store);
        auto tiles = makeTiles();
        std::swap(tiles[0], tiles[2]);
        store.replace("tiles", tiles);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute());
        EXPECT_NEAR(step.getOutput().value[0].position.x, 2 * SHIFT, TOLERANCE);
        EXPECT_NEAR(step.getOutput().value[2].position.x, 0.0, TOLERANCE);
    }

    TEST(AbsolutePosStep, BuildsTypedPipelineWithExistingMatcher) {
        auto script = nlohmann::json{{"pipeline", {{"steps", {
            {{"name", "import"}}, {{"name", "phase-correlation"}}, {{"name", "absolute-pos"}}
        }}}}};
        auto pipeline = PipelineNs::buildPipeline(script);
        EXPECT_EQ(pipeline.getSteps().size(), 3U);
        EXPECT_EQ(pipeline.getSteps()[2]->getConfigJson()["name"], "absolute-pos");
        EXPECT_THROW(PipelineNs::buildPipeline({{"pipeline", {{"steps", {{{"name", "absolute-pos"}}}}}}}), std::invalid_argument);
    }

    TEST(AbsolutePosStep, PublishesResidualOfEachPairAfterAdjustment) {
        StepNs::DataStoreClass store;
        fillStore(store);
        store.replace("matches", std::vector<DataNs::PairMatches>{makePair(0, 1), makePair(1, 2), makePair(0, 2, {INCONSISTENT_SHIFT, 0.0f})});
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult();
        const auto residuals = store.get<std::vector<DataNs::PairResidual>>("pairResiduals");
        ASSERT_EQ(residuals.size(), 3U);
        EXPECT_EQ(residuals[2].tileIdA, 0);
        EXPECT_EQ(residuals[2].tileIdB, 2);
        EXPECT_NEAR(residuals[0].residual.x, -1.0, TOLERANCE);   // 10 − 11
        EXPECT_NEAR(residuals[1].residual.x, -1.0, TOLERANCE);   // 10 − 11
        EXPECT_NEAR(residuals[2].residual.x, 1.0, TOLERANCE);    // 23 − 22
        EXPECT_NEAR(residuals[2].residualNorm, 1.0, TOLERANCE);
        EXPECT_EQ(step.getConfigJson()["outputs"][1]["dataType"], "PairResiduals");
    }

    TEST(AbsolutePosStep, RestoresResidualsFromCheckpoint) {
        TestDirectory directory;
        StepNs::DataStoreClass store;
        fillStore(store);
        StepsNs::AbsolutePosStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute());
        CheckpointNs::CheckpointClass checkpoint(directory.path);
        std::string failureReason;
        ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
        StepNs::DataStoreClass restored;
        checkpoint.load(0, step, "pairResiduals", "PairResiduals", restored);
        const auto residuals = restored.get<std::vector<DataNs::PairResidual>>("pairResiduals");
        ASSERT_EQ(residuals.size(), 2U);
        EXPECT_EQ(residuals[1].tileIdB, 2);
        EXPECT_NEAR(residuals[1].residualNorm, 0.0, TOLERANCE);
    }

}
