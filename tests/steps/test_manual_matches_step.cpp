// tests/steps/test_manual_matches_step.cpp
// Step manual-matches (tâche 7) : remplace les points des paires corrigées à la main et recalcule leur translation
// par la médiane, ajoute une paire que l'appariement a écartée, laisse les autres paires inchangées, refuse une
// correction invalide sans toucher à matches.

#include "steps/manual_matches_step.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/absolute_pos_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
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
    constexpr int TILE_WIDTH = 1000;
    constexpr int TILE_HEIGHT = 800;
    constexpr int UNKNOWN_TILE_ID = 9;

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

    // Grille de 2 × 2 tuiles, identifiants ligne par ligne :
    //   0 1
    //   2 3
    DataNs::TileList makeGrid() {
        DataNs::TileList tiles;
        for (int tileId = 0; tileId < 4; ++tileId) {
            const std::string name = "tile_" + std::to_string(tileId) + ".tif";
            tiles.push_back(std::make_shared<DataNs::TileClass>(tileId, name, "cache.tif", TILE_WIDTH, TILE_HEIGHT, CV_8UC1));
            tiles.back()->setGridPosition(tileId / 2, tileId % 2);
        }
        tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, tiles[2].get());
        tiles[1]->setNeighbours(tiles[0].get(), nullptr, nullptr, tiles[3].get());
        tiles[2]->setNeighbours(nullptr, tiles[3].get(), tiles[0].get(), nullptr);
        tiles[3]->setNeighbours(tiles[2].get(), nullptr, tiles[1].get(), nullptr);
        return tiles;
    }

    // Paire telle que l'appariement la produit : MATCHED_POINT_COUNT points décalés de (MATCHED_SHIFT_X, MATCHED_SHIFT_Y).
    DataNs::PairMatches makeMatchedPair(int tileIdA, int tileIdB, DataNs::Direction direction) {
        DataNs::PairMatches pair;
        pair.tileIdA = tileIdA;
        pair.tileIdB = tileIdB;
        pair.direction = direction;
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

    // Les paires (1, 3) et (2, 3) ont été écartées par l'appariement : la tuile 3 n'est reliée à aucune autre.
    std::vector<DataNs::PairMatches> twoPairs() {
        return {makeMatchedPair(0, 1, DataNs::Direction::Right), makeMatchedPair(0, 2, DataNs::Direction::Bottom)};
    }

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

    // La même correction saisie dans l'autre sens : B devient A, avec ses points.
    Json reversed(const Json& correction) {
        return {{"tileIdA", correction.at("tileIdB")}, {"tileIdB", correction.at("tileIdA")},
                {"pointsInA", correction.at("pointsInB")}, {"pointsInB", correction.at("pointsInA")}};
    }

    // La paire n'a pas bougé : mêmes points, mêmes scores, même translation que makeMatchedPair.
    void expectUnchanged(const DataNs::PairMatches& pair) {
        EXPECT_EQ(pair.featureMatches.pointsInA.size(), static_cast<std::size_t>(MATCHED_POINT_COUNT));
        EXPECT_NEAR(pair.featureMatches.translationX, MATCHED_SHIFT_X, TOLERANCE);
        EXPECT_NEAR(pair.featureMatches.translationY, MATCHED_SHIFT_Y, TOLERANCE);
        for (const float score : pair.featureMatches.confidenceScores) EXPECT_NEAR(score, MATCH_SCORE, TOLERANCE);
    }

    // La paire a pris la translation saisie à la main, malgré le point faux, et ses points ont confiance 1.
    void expectCorrected(const DataNs::PairMatches& pair) {
        EXPECT_NEAR(pair.featureMatches.translationX, TRUE_SHIFT_X, TOLERANCE);
        EXPECT_NEAR(pair.featureMatches.translationY, TRUE_SHIFT_Y, TOLERANCE);
        ASSERT_EQ(pair.featureMatches.pointsInA.size(), 3U);
        ASSERT_EQ(pair.featureMatches.pointsInB.size(), 3U);
        for (const float score : pair.featureMatches.confidenceScores) EXPECT_NEAR(score, 1.0F, TOLERANCE);
    }

    void fillStore(StepNs::DataStoreClass& store) {
        store.replace("tiles", makeGrid());
        store.replace("matches", twoPairs());
    }

    // Exécute manual-matches avec ces corrections sur la grille et ses deux paires ; renvoie le résultat de execute().
    bool runWithCorrections(const Json& corrections, StepNs::DataStoreClass& store, Json& stepResult) {
        fillStore(store);
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
    expectCorrected(pairs.at(0));
    expectUnchanged(pairs.at(1));
    ASSERT_EQ(stepResult.at("correctedPairs").size(), 1U);
    EXPECT_FALSE(stepResult.at("correctedPairs").at(0).at("added").get<bool>());
}

// Une paire écartée par l'appariement est ajoutée à la fin de matches, avec la direction de B par rapport à A.
TEST(ManualMatchesStep, AddsAPairMissingFromMatches) {
    StepNs::DataStoreClass store;
    Json stepResult;
    ASSERT_TRUE(runWithCorrections(Json::array({correctionWithOneWrongPoint(1, 3)}), store, stepResult))
        << stepResult.dump();

    const auto& pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(pairs.size(), 3U);
    expectUnchanged(pairs.at(0));
    expectUnchanged(pairs.at(1));
    const DataNs::PairMatches& added = pairs.at(2);
    EXPECT_EQ(added.tileIdA, 1);
    EXPECT_EQ(added.tileIdB, 3);
    EXPECT_EQ(added.direction, DataNs::Direction::Bottom);
    expectCorrected(added);
    EXPECT_TRUE(stepResult.at("correctedPairs").at(0).at("added").get<bool>());
}

// Une correction saisie dans l'autre sens, (1, 0) pour la paire (0, 1), corrige la paire existante sans en ajouter.
TEST(ManualMatchesStep, ReversedCorrectionUpdatesTheExistingPair) {
    StepNs::DataStoreClass store;
    Json stepResult;
    ASSERT_TRUE(runWithCorrections(Json::array({reversed(correctionWithOneWrongPoint(0, 1))}), store, stepResult))
        << stepResult.dump();

    const auto& pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(pairs.size(), 2U);
    EXPECT_EQ(pairs.at(0).tileIdA, 0);
    EXPECT_EQ(pairs.at(0).tileIdB, 1);
    expectCorrected(pairs.at(0));
    expectUnchanged(pairs.at(1));
    EXPECT_EQ(stepResult.at("correctedPairs").at(0).at("idA").get<int>(), 0);
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
        Json::array({correctionWithOneWrongPoint(0, UNKNOWN_TILE_ID)}),   // tuile absente de tiles
        Json::array({correctionWithOneWrongPoint(0, 3)}),                 // tuiles en diagonale, non voisines
        Json::array({correctionWithOneWrongPoint(1, 1)}),                 // deux fois la même tuile
        Json::array({valid, valid}),                                      // paire corrigée deux fois
        Json::array({valid, reversed(valid)}),                            // la même, une fois dans chaque sens
        Json::array({emptyPoints}),
        Json::array({correctionWithOneWrongPoint(1, 3), emptyPoints}),    // une paire ajoutée, puis une erreur
        Json::array({differentSizes}),
        Json::array({malformedPoint}),
        Json::object(),                                                   // corrections n'est pas un tableau
    };
    for (const Json& corrections : invalidCorrections) {
        StepNs::DataStoreClass store;
        Json stepResult;
        EXPECT_FALSE(runWithCorrections(corrections, store, stepResult)) << corrections.dump();
        EXPECT_FALSE(stepResult.at("error").get<std::string>().empty()) << corrections.dump();
        const auto& pairs = store.get<std::vector<DataNs::PairMatches>>("matches");
        EXPECT_EQ(pairs.size(), 2U) << corrections.dump();
        for (const DataNs::PairMatches& pair : pairs) expectUnchanged(pair);
    }
}

// Cas de la tâche 7 : la tuile 3 a perdu toutes ses paires, absolute-pos échoue. La paire (1, 3) saisie à la main
// la relie de nouveau, et absolute-pos la place à la position de la tuile 1 plus la translation saisie.
TEST(ManualMatchesStep, AddedPairReconnectsATileForAbsolutePos) {
    StepNs::DataStoreClass store;
    fillStore(store);
    StepsNs::AbsolutePosStepClass absolutePos;
    absolutePos.setDataStore(store);
    ASSERT_FALSE(absolutePos.execute());

    StepsNs::ManualMatchesStepClass manualMatches(
        StepsNs::ManualMatchesInput{Json::array({correctionWithOneWrongPoint(1, 3)})});
    manualMatches.setDataStore(store);
    ASSERT_TRUE(manualMatches.execute()) << manualMatches.getResult().dump();
    ASSERT_TRUE(absolutePos.execute()) << absolutePos.getResult().dump();

    const auto& positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
    ASSERT_EQ(positions.size(), 4U);
    EXPECT_EQ(positions.at(3).tileId, 3);
    EXPECT_NEAR(positions.at(3).position.x, MATCHED_SHIFT_X + TRUE_SHIFT_X, TOLERANCE);
    EXPECT_NEAR(positions.at(3).position.y, MATCHED_SHIFT_Y + TRUE_SHIFT_Y, TOLERANCE);
}

// Reprise : modifier une correction invalide le checkpoint de manual-matches, donc ce step et les suivants sont
// refaits, sans refaire l'import ni l'appariement (leurs checkpoints ne dépendent pas des corrections).
TEST(ManualMatchesStep, ChangedCorrectionInvalidatesItsCheckpoint) {
    TestDirectory directory;
    StepNs::DataStoreClass store;
    fillStore(store);
    StepsNs::ManualMatchesStepClass step(StepsNs::ManualMatchesInput{Json::array({correctionWithOneWrongPoint(0, 1)})});
    step.setDataStore(store);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();

    const CheckpointNs::CheckpointClass checkpoint(directory.path);
    std::string failureReason;
    ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
    EXPECT_TRUE(checkpoint.valid(0, step));

    const StepsNs::ManualMatchesStepClass changedStep(
        StepsNs::ManualMatchesInput{Json::array({correctionWithOneWrongPoint(1, 3)})});
    EXPECT_FALSE(checkpoint.valid(0, changedStep));

    StepNs::DataStoreClass restored;
    checkpoint.load(0, step, "matches", "PairMatches", restored);
    EXPECT_NEAR(restored.get<std::vector<DataNs::PairMatches>>("matches").at(0).featureMatches.translationX,
                TRUE_SHIFT_X, TOLERANCE);
}
