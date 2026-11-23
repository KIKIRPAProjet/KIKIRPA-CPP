// tests/steps/test_manual_matches_step.cpp
// Step manual-matches (tâche 7) : remplace les points des paires corrigées à la main et recalcule leur translation
// par la médiane, laisse les autres paires inchangées, refuse une correction invalide sans toucher à matches.

#include "steps/manual_matches_step.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/types.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

    using Json = nlohmann::json;

    // Translation trouvée par l'appariement, et translation juste saisie à la main.
    constexpr float MATCHED_SHIFT_X = 320.0F;
    constexpr float MATCHED_SHIFT_Y = 0.0F;
    constexpr float TRUE_SHIFT_X = 400.0F;
    constexpr float TRUE_SHIFT_Y = 7.0F;
    // Point volontairement faux : la médiane doit l'ignorer.
    constexpr float WRONG_SHIFT_X = 900.0F;
    constexpr float WRONG_SHIFT_Y = 300.0F;
    constexpr float POINT_X = 10.0F;
    constexpr float POINT_Y = 20.0F;
    constexpr float POINT_STEP = 50.0F;
    constexpr float MATCH_SCORE = 0.5F;
    constexpr int MATCHED_POINT_COUNT = 4;
    constexpr double TOLERANCE = 1e-4;

    struct TestDirectory {
        std::filesystem::path path;
        TestDirectory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("manual-matches-step-test-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
        TestDirectory(TestDirectory&&) = delete;
        TestDirectory& operator=(TestDirectory&&) = delete;
    };

    // Paire telle que l'appariement la produit : MATCHED_POINT_COUNT points décalés de (MATCHED_SHIFT_X, MATCHED_SHIFT_Y).
    DataNs::PairMatches makeMatchedPair(int tileIdA, int tileIdB) {
        DataNs::PairMatches pair;
        pair.tileIdA = tileIdA;
        pair.tileIdB = tileIdB;
        for (int index = 0; index < MATCHED_POINT_COUNT; ++index) {
            const cv::Point2f pointB(POINT_X + (static_cast<float>(index) * POINT_STEP), POINT_Y);
            pair.featureMatches.pointsInB.push_back(pointB);
            pair.featureMatches.pointsInA.emplace_back(pointB.x + MATCHED_SHIFT_X, pointB.y + MATCHED_SHIFT_Y);
            pair.featureMatches.confidenceScores.push_back(MATCH_SCORE);
        }
        pair.featureMatches.translationX = MATCHED_SHIFT_X;
        pair.featureMatches.translationY = MATCHED_SHIFT_Y;
        return pair;
    }

    std::vector<DataNs::PairMatches> twoPairs() { return {makeMatchedPair(0, 1), makeMatchedPair(1, 2)}; }

    // Correction de la paire (tileIdA, tileIdB) : deux points justes et un point faux.
    Json correctionWithOneWrongPoint(int tileIdA, int tileIdB) {
        Json pointsInA = Json::array();
        Json pointsInB = Json::array();
        for (int index = 0; index < 2; ++index) {
            const float pointBX = POINT_X + (static_cast<float>(index) * POINT_STEP);
            pointsInB.push_back({pointBX, POINT_Y});
            pointsInA.push_back({pointBX + TRUE_SHIFT_X, POINT_Y + TRUE_SHIFT_Y});
        }
        pointsInB.push_back({POINT_X, POINT_Y});
        pointsInA.push_back({POINT_X + WRONG_SHIFT_X, POINT_Y + WRONG_SHIFT_Y});
        return {{"tileIdA", tileIdA}, {"tileIdB", tileIdB}, {"pointsInA", pointsInA}, {"pointsInB", pointsInB}};
    }

    // La paire n'a pas bougé : mêmes points, mêmes scores, même translation que makeMatchedPair.
    void expectUnchanged(const DataNs::PairMatches& pair) {
        EXPECT_EQ(pair.featureMatches.pointsInA.size(), static_cast<std::size_t>(MATCHED_POINT_COUNT));
        EXPECT_NEAR(pair.featureMatches.translationX, MATCHED_SHIFT_X, TOLERANCE);
        EXPECT_NEAR(pair.featureMatches.translationY, MATCHED_SHIFT_Y, TOLERANCE);
        for (const float score : pair.featureMatches.confidenceScores) EXPECT_NEAR(score, MATCH_SCORE, TOLERANCE);
    }

    // Exécute manual-matches avec ces corrections sur deux paires ; renvoie le résultat de execute().
    bool runWithCorrections(const Json& corrections, StepNs::DataStoreClass& store, Json& stepResult) {
        store.replace("matches", twoPairs());
        StepsNs::ManualMatchesStepClass step(StepsNs::ManualMatchesInput{corrections});
        step.setDataStore(store);
        const bool succeeded = step.execute();
        stepResult = step.getResult();
        return succeeded;
    }

}

// Médiane : le point faux ne déplace pas la tuile. Seule la paire corrigée change.
TEST(ManualMatchesStep, MedianIgnoresOneWrongPointAndKeepsOtherPairs) {
    StepNs::DataStoreClass store;
    Json stepResult;
    ASSERT_TRUE(runWithCorrections(Json::array({correctionWithOneWrongPoint(0, 1)}), store, stepResult))
        << stepResult.dump();

    const auto& pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(pairs.size(), 2U);
    const DataNs::PairMatches& corrected = pairs.at(0);
    EXPECT_NEAR(corrected.featureMatches.translationX, TRUE_SHIFT_X, TOLERANCE);
    EXPECT_NEAR(corrected.featureMatches.translationY, TRUE_SHIFT_Y, TOLERANCE);
    ASSERT_EQ(corrected.featureMatches.pointsInA.size(), 3U);
    for (const float score : corrected.featureMatches.confidenceScores) EXPECT_NEAR(score, 1.0F, TOLERANCE);
    expectUnchanged(pairs.at(1));
    EXPECT_EQ(stepResult.at("correctedPairs").size(), 1U);
}

// Sans correction, matches est transmis tel quel.
TEST(ManualMatchesStep, EmptyCorrectionsPassMatchesThrough) {
    StepNs::DataStoreClass store;
    Json stepResult;
    ASSERT_TRUE(runWithCorrections(Json::array(), store, stepResult)) << stepResult.dump();
    const auto& pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(pairs.size(), 2U);
    for (const DataNs::PairMatches& pair : pairs) expectUnchanged(pair);
}

// Une correction invalide fait échouer le step avec un message, sans modifier matches.
TEST(ManualMatchesStep, RejectsInvalidCorrectionsWithoutChangingMatches) {
    const Json valid = correctionWithOneWrongPoint(0, 1);
    Json emptyPoints = valid;
    emptyPoints["pointsInA"] = Json::array();
    Json differentSizes = valid;
    differentSizes["pointsInB"].erase(0);
    Json malformedPoint = valid;
    malformedPoint["pointsInA"][0] = {POINT_X};

    const std::vector<Json> invalidCorrections = {
        Json::array({correctionWithOneWrongPoint(0, 2)}),   // paire absente de matches
        Json::array({valid, valid}),                        // paire corrigée deux fois
        Json::array({emptyPoints}),
        Json::array({differentSizes}),
        Json::array({malformedPoint}),
        Json::object(),                                     // corrections n'est pas un tableau
    };
    for (const Json& corrections : invalidCorrections) {
        StepNs::DataStoreClass store;
        Json stepResult;
        EXPECT_FALSE(runWithCorrections(corrections, store, stepResult)) << corrections.dump();
        EXPECT_FALSE(stepResult.at("error").get<std::string>().empty()) << corrections.dump();
        for (const DataNs::PairMatches& pair : store.get<std::vector<DataNs::PairMatches>>("matches"))
            expectUnchanged(pair);
    }
}

// Reprise : modifier une correction invalide le checkpoint de manual-matches, donc ce step et les suivants sont
// refaits, sans refaire l'import ni l'appariement (leurs checkpoints ne dépendent pas des corrections).
TEST(ManualMatchesStep, ChangedCorrectionInvalidatesItsCheckpoint) {
    TestDirectory directory;
    StepNs::DataStoreClass store;
    store.replace("matches", twoPairs());
    StepsNs::ManualMatchesStepClass step(StepsNs::ManualMatchesInput{Json::array({correctionWithOneWrongPoint(0, 1)})});
    step.setDataStore(store);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();

    const CheckpointNs::CheckpointClass checkpoint(directory.path);
    std::string failureReason;
    ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
    EXPECT_TRUE(checkpoint.valid(0, step));

    const StepsNs::ManualMatchesStepClass changedStep(
        StepsNs::ManualMatchesInput{Json::array({correctionWithOneWrongPoint(1, 2)})});
    EXPECT_FALSE(checkpoint.valid(0, changedStep));

    StepNs::DataStoreClass restored;
    checkpoint.load(0, step, "matches", "PairMatches", restored);
    EXPECT_NEAR(restored.get<std::vector<DataNs::PairMatches>>("matches").at(0).featureMatches.translationX,
                TRUE_SHIFT_X, TOLERANCE);
}
