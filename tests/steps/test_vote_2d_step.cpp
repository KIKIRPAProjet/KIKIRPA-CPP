// tests/steps/test_vote_2d_step.cpp
#include "steps/vote_2d_step.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/algorithm_types.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/types.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

    constexpr int INLIER_COUNT = 6;
    constexpr int OUTLIER_COUNT = 2;
    constexpr int HIGH_MIN_INLIERS = 50;
    constexpr float TRUE_SHIFT_X = 50.0f;
    constexpr float TRUE_SHIFT_Y = -30.0f;
    constexpr float OUTLIER_SHIFT_X = 240.0f;
    constexpr float OUTLIER_SHIFT_Y = 180.0f;
    constexpr float BASE_COORDINATE_X = 200.0f;
    constexpr float BASE_COORDINATE_Y = 150.0f;
    constexpr float COORDINATE_STEP = 10.0f;
    constexpr float INLIER_SCORE = 0.9f;
    constexpr float OUTLIER_SCORE = 0.2f;
    constexpr float SPREAD_OFFSET = 2.0f;
    constexpr float RELAXED_THRESHOLD = 3.5f;
    constexpr float INVALID_NEGATIVE = -1.0f;
    constexpr double TOLERANCE = 1e-4;

    struct TestDirectory {
        std::filesystem::path path;
        TestDirectory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("vote-2d-step-test-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    struct PairFixture {
        int tileIdA = 0;
        int tileIdB = 1;
        int inlierCount = INLIER_COUNT;
        int outlierCount = OUTLIER_COUNT;
    };

    DataNs::PairMatches makePairWithOutliers(const PairFixture& fixture = {}) {
        DataNs::PairMatches pair;
        pair.tileIdA = fixture.tileIdA;
        pair.tileIdB = fixture.tileIdB;
        pair.direction = DataNs::Direction::Right;
        for (int index = 0; index < fixture.inlierCount; ++index) {
            const float offset = static_cast<float>(index) * COORDINATE_STEP;
            const cv::Point2f pointB(BASE_COORDINATE_X + offset, BASE_COORDINATE_Y + offset);
            pair.featureMatches.pointsInB.push_back(pointB);
            pair.featureMatches.pointsInA.emplace_back(pointB.x + TRUE_SHIFT_X, pointB.y + TRUE_SHIFT_Y);
            pair.featureMatches.confidenceScores.push_back(INLIER_SCORE);
        }
        for (int index = 0; index < fixture.outlierCount; ++index) {
            const float offset = static_cast<float>(index) * COORDINATE_STEP;
            const cv::Point2f pointB(BASE_COORDINATE_X + offset, BASE_COORDINATE_Y);
            pair.featureMatches.pointsInB.push_back(pointB);
            pair.featureMatches.pointsInA.emplace_back(pointB.x + OUTLIER_SHIFT_X + offset,
                                                       pointB.y + OUTLIER_SHIFT_Y - offset);
            pair.featureMatches.confidenceScores.push_back(OUTLIER_SCORE);
        }
        return pair;
    }

    std::vector<DataNs::PairMatches> twoPairs() {
        return {makePairWithOutliers({0, 1, INLIER_COUNT, OUTLIER_COUNT}),
                makePairWithOutliers({1, 2, 1, OUTLIER_COUNT})};
    }

    // Test 1 : Vérifie le filtrage nominal des points aberrants, la mise à jour de la translation (dx, dy) et le retrait des paires invalides.
    TEST(Vote2DStep, FiltersOutliersAndUpdatesTranslationInDataStore) {
        StepNs::DataStoreClass store;
        store.replace("matches", twoPairs());
        StepsNs::Vote2DStepClass step;
        step.setDataStore(store);

        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        const auto& kept = store.get<std::vector<DataNs::PairMatches>>("matches");
        ASSERT_EQ(kept.size(), 1U);
        EXPECT_EQ(kept[0].tileIdA, 0);
        EXPECT_EQ(kept[0].tileIdB, 1);
        EXPECT_EQ(kept[0].featureMatches.pointsInA.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_EQ(kept[0].featureMatches.pointsInB.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_EQ(kept[0].featureMatches.confidenceScores.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_NEAR(kept[0].featureMatches.translationX, TRUE_SHIFT_X, TOLERANCE);
        EXPECT_NEAR(kept[0].featureMatches.translationY, TRUE_SHIFT_Y, TOLERANCE);
        EXPECT_EQ(step.getResult().at("kept").get<int>(), 1);
        EXPECT_EQ(step.getResult().at("removed").get<int>(), 1);
        EXPECT_EQ(step.getResult().at("failures").size(), 1U);
    }

    // Test 2 : Vérifie l'échec propre (execute() == false avec message d'erreur) et la préservation de l'entrée si aucune paire n'a assez d'inliers.
    TEST(Vote2DStep, FailsWhenNoPairHasEnoughInliersAndPreservesInput) {
        StepNs::DataStoreClass store;
        store.replace("matches", twoPairs());
        StepsNs::Vote2DInput input;
        input.minInliers = HIGH_MIN_INLIERS;
        StepsNs::Vote2DStepClass step(input);
        step.setDataStore(store);

        EXPECT_FALSE(step.execute());
        EXPECT_FALSE(step.getResult().at("error").get<std::string>().empty());
        EXPECT_TRUE(step.getOutput().value.empty());
        EXPECT_EQ(store.get<std::vector<DataNs::PairMatches>>("matches").size(), 2U);
    }

    // Test 3 : Vérifie la prise en compte des paramètres injectés depuis le script JSON pour remplacer les valeurs par défaut.
    TEST(Vote2DStep, ReadsParametersFromScript) {
        DataNs::PairMatches pair = makePairWithOutliers({0, 1, 4, 0});
        pair.featureMatches.pointsInA[0].x += SPREAD_OFFSET;
        pair.featureMatches.pointsInA[1].x -= SPREAD_OFFSET;

        StepNs::DataStoreClass store;
        store.replace("matches", std::vector<DataNs::PairMatches>{pair});
        StepsNs::Vote2DStepClass step;
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());

        step.importConfig({{"name", "vote-2d"},
                           {"parameters", {{{"valueType", "float"}, {"name", "seuil"}, {"value", RELAXED_THRESHOLD}},
                                           {{"valueType", "float"}, {"name", "case"}, {"value", RELAXED_THRESHOLD}},
                                           {{"valueType", "int"}, {"name", "minMatches"}, {"value", 3}},
                                           {{"valueType", "int"}, {"name", "minInliers"}, {"value", 4}}}}});
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        EXPECT_EQ(store.get<std::vector<DataNs::PairMatches>>("matches").at(0).featureMatches.pointsInA.size(), 4U);
    }

    // Test 4 : Vérifie la validation des entrées (DataStore absent ou vide) et le rejet des paramètres hors bornes (seuil <= 0, case <= 0, minMatches < 2).
    TEST(Vote2DStep, RejectsInvalidParametersAndMissingInputs) {
        StepsNs::Vote2DStepClass stepWithoutStore;
        EXPECT_FALSE(stepWithoutStore.execute());

        StepNs::DataStoreClass emptyStore;
        stepWithoutStore.setDataStore(emptyStore);
        EXPECT_FALSE(stepWithoutStore.execute());

        StepNs::DataStoreClass store;
        store.replace("matches", twoPairs());
        StepsNs::Vote2DStepClass invalidSeuil(StepsNs::Vote2DInput{INVALID_NEGATIVE, 1.0f, 3, 3});
        invalidSeuil.setDataStore(store);
        EXPECT_FALSE(invalidSeuil.execute());

        StepsNs::Vote2DStepClass invalidCase(StepsNs::Vote2DInput{1.0f, 0.0f, 3, 3});
        invalidCase.setDataStore(store);
        EXPECT_FALSE(invalidCase.execute());

        StepsNs::Vote2DStepClass invalidMinMatches(StepsNs::Vote2DInput{1.0f, 1.0f, 1, 3});
        invalidMinMatches.setDataStore(store);
        EXPECT_FALSE(invalidMinMatches.execute());
    }

    // Test 5 : Vérifie l'élimination des points non finis (NaN / Inf) et le rejet des paires dont les scores de confiance sont désynchronisés.
    TEST(Vote2DStep, FiltersNonFinitePointsAndRejectsMismatchedScores) {
        DataNs::PairMatches pairWithNan = makePairWithOutliers({0, 1, INLIER_COUNT, 0});
        pairWithNan.featureMatches.pointsInA.emplace_back(std::numeric_limits<float>::quiet_NaN(), 0.0f);
        pairWithNan.featureMatches.pointsInB.emplace_back(BASE_COORDINATE_X, BASE_COORDINATE_Y);
        pairWithNan.featureMatches.confidenceScores.push_back(OUTLIER_SCORE);
        pairWithNan.featureMatches.pointsInA.emplace_back(BASE_COORDINATE_X, std::numeric_limits<float>::infinity());
        pairWithNan.featureMatches.pointsInB.emplace_back(BASE_COORDINATE_X, BASE_COORDINATE_Y);
        pairWithNan.featureMatches.confidenceScores.push_back(OUTLIER_SCORE);

        DataNs::PairMatches mismatchedScores = makePairWithOutliers({1, 2, INLIER_COUNT, 0});
        mismatchedScores.featureMatches.confidenceScores.pop_back();

        StepNs::DataStoreClass store;
        store.replace("matches", std::vector<DataNs::PairMatches>{pairWithNan, mismatchedScores});
        StepsNs::Vote2DStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        const auto& kept = store.get<std::vector<DataNs::PairMatches>>("matches");
        ASSERT_EQ(kept.size(), 1U);
        EXPECT_EQ(kept[0].featureMatches.pointsInA.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_EQ(kept[0].featureMatches.confidenceScores.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_EQ(step.getResult().at("removed").get<int>(), 1);
    }

    // Test 6 : Vérifie le contrôle structurel par buildPipeline (refus si aucune étape ne produit 'matches', acceptation après un matcher).
    TEST(Vote2DStep, IsRefusedWithoutAMatchingStepAndAcceptedAfterMatcher) {
        const nlohmann::json invalidScript = {
            {"pipeline", {{"steps", nlohmann::json::array({{{"name", "import"}}, {{"name", "vote-2d"}}})}}}};
        EXPECT_THROW(PipelineNs::buildPipeline(invalidScript), std::invalid_argument);

        const nlohmann::json validScript = {
            {"pipeline",
             {{"steps",
               nlohmann::json::array(
                   {{{"name", "import"}}, {{"name", "phase-correlation"}}, {{"name", "vote-2d"}}, {{"name", "absolute-pos"}}})}}}};
        const PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(validScript);
        EXPECT_EQ(pipeline.getSteps().size(), 4U);
        EXPECT_EQ(PipelineNs::buildPipeline(pipeline.exportConfig()).exportConfig(), pipeline.exportConfig());
    }

    // Test 7 : Vérifie la sauvegarde sur disque et la restauration fidèle des paires filtrées via le système de Checkpoint.
    TEST(Vote2DStep, RestoresFilteredMatchesFromCheckpoint) {
        TestDirectory directory;
        StepNs::DataStoreClass store;
        store.replace("matches", twoPairs());
        StepsNs::Vote2DStepClass step;
        step.setDataStore(store);
        ASSERT_TRUE(step.execute());

        CheckpointNs::CheckpointClass checkpoint(directory.path);
        std::string failureReason;
        ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
        ASSERT_TRUE(checkpoint.valid(0, step));

        StepNs::DataStoreClass restored;
        checkpoint.load(0, step, "matches", "PairMatches", restored);
        const auto& pairs = restored.get<std::vector<DataNs::PairMatches>>("matches");
        ASSERT_EQ(pairs.size(), 1U);
        EXPECT_EQ(pairs[0].featureMatches.pointsInA.size(), static_cast<std::size_t>(INLIER_COUNT));
        EXPECT_NEAR(pairs[0].featureMatches.translationX, TRUE_SHIFT_X, TOLERANCE);
        EXPECT_NEAR(pairs[0].featureMatches.translationY, TRUE_SHIFT_Y, TOLERANCE);
    }

}
