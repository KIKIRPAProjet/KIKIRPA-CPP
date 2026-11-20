// tests/steps/test_overlap_calibration.cpp
// Recouvrements horizontal et vertical distincts, marge du rail et recalage du recouvrement nominal
// (steps/matching_step_utils.h : calibrateOverlap ; steps phase-correlation, overlap-estimation, sift).
// GoogleTest, ctest -L unit. Grille 2 × 2 de tuiles générées : 20 % de recouvrement en horizontal, 27 % en vertical,
// alors que le script annonce 20 % partout. À 1 000 px de côté, la marge automatique de la corrélation de phase est de
// 50 px : les paires verticales sont à 70 px du nominal, hors de la marge (cas de 02-Philadelphia, 26 % pour 20 %).

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/matching_step_utils.h"
#include "steps/overlap_estimation_step.h"
#include "steps/phase_correlation_step.h"
#include "steps/sift_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

    using DataNs::Direction;
    using StepsNs::OverlapObservation;

    constexpr int TILE_SIDE = 1000;
    constexpr int HORIZONTAL_STEP = 800;    // recouvrement horizontal réel : 20 %
    constexpr int VERTICAL_STEP = 730;      // recouvrement vertical réel : 27 %
    constexpr double VERTICAL_OVERLAP = 0.27;
    constexpr int TEXTURE_SEED = 7;
    constexpr int TEXTURE_MIN = 20;
    constexpr int TEXTURE_MAX = 220;
    constexpr double TEXTURE_SIGMA = 1.0;
    constexpr double TRANSLATION_TOLERANCE = 1.0;   // px
    constexpr double OVERLAP_TOLERANCE = 0.005;

    void require(bool ok, const std::string& message) {
        if (!ok) throw std::runtime_error(message);
    }

    // Dossier temporaire isolé, supprimé à la destruction.
    struct Directory {
        std::filesystem::path path;
        Directory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("overlap-calibration-" + std::to_string(stamp));
            require(std::filesystem::create_directory(path), "create isolated test directory");
        }
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
        Directory(const Directory&) = delete;
        Directory& operator=(const Directory&) = delete;
    };

    // Grille de tuiles découpées dans une même texture aléatoire, voisines reliées, identifiants ligne par ligne.
    DataNs::TileList makeGrid(const Directory& directory, int rows, int columns) {
        cv::Mat source(VERTICAL_STEP * (rows - 1) + TILE_SIDE, HORIZONTAL_STEP * (columns - 1) + TILE_SIDE, CV_8UC1);
        cv::RNG random(TEXTURE_SEED);
        random.fill(source, cv::RNG::UNIFORM, TEXTURE_MIN, TEXTURE_MAX);
        cv::GaussianBlur(source, source, cv::Size(3, 3), TEXTURE_SIGMA);

        DataNs::TileList tiles;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const auto file = directory.path / ("tile_" + std::to_string(row) + "_" + std::to_string(column) + ".tif");
                const cv::Mat image =
                    source(cv::Rect(column * HORIZONTAL_STEP, row * VERTICAL_STEP, TILE_SIDE, TILE_SIDE)).clone();
                require(cv::imwrite(file.string(), image), "write tile fixture");
                tiles.push_back(std::make_shared<DataNs::TileClass>(row * columns + column, file, std::filesystem::path{},
                                                                    image.cols, image.rows, image.type()));
            }
        }
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                auto at = [&](int r, int c) {
                    return r >= 0 && r < rows && c >= 0 && c < columns ? tiles[r * columns + c].get() : nullptr;
                };
                tiles[row * columns + column]->setNeighbours(at(row, column - 1), at(row, column + 1), at(row - 1, column),
                                                             at(row + 1, column));
            }
        }
        return tiles;
    }

    const nlohmann::json* findCalibration(const nlohmann::json& result, const std::string& direction) {
        if (!result.contains("overlapCalibration")) return nullptr;
        for (const auto& entry : result.at("overlapCalibration"))
            if (entry.at("dir") == direction) return &entry;
        return nullptr;
    }

    // === Outils communs ===

    TEST(OverlapCalibrationTest, OverlapForDirectionUsesVerticalOverlapForVerticalPairs) {
        EXPECT_FLOAT_EQ(StepsNs::overlapForDirection(Direction::Right, 0.2f, 0.27f), 0.2f);
        EXPECT_FLOAT_EQ(StepsNs::overlapForDirection(Direction::Left, 0.2f, 0.27f), 0.2f);
        EXPECT_FLOAT_EQ(StepsNs::overlapForDirection(Direction::Bottom, 0.2f, 0.27f), 0.27f);
        EXPECT_FLOAT_EQ(StepsNs::overlapForDirection(Direction::Top, 0.2f, 0.27f), 0.27f);
        EXPECT_FLOAT_EQ(StepsNs::overlapForDirection(Direction::Bottom, 0.2f, 0.f), 0.2f);   // 0 : comme overlap
    }

    TEST(OverlapCalibrationTest, CheckOverlapsRejectsOutOfRangeValues) {
        EXPECT_TRUE(StepsNs::checkOverlaps(0.2f, 0.f).empty());
        EXPECT_TRUE(StepsNs::checkOverlaps(0.2f, 0.27f).empty());
        EXPECT_NE(StepsNs::checkOverlaps(0.f, 0.27f).find("overlap"), std::string::npos);
        EXPECT_NE(StepsNs::checkOverlaps(0.2f, 1.2f).find("verticalOverlap"), std::string::npos);
        EXPECT_NE(StepsNs::checkOverlaps(0.2f, -0.1f).find("verticalOverlap"), std::string::npos);
    }

    TEST(OverlapCalibrationTest, MeasuredOverlapFractionFollowsTheDirection) {
        EXPECT_DOUBLE_EQ(StepsNs::measuredOverlapFraction(Direction::Right, {1000, 500}, {800, -3}), 0.2);
        EXPECT_DOUBLE_EQ(StepsNs::measuredOverlapFraction(Direction::Bottom, {1000, 500}, {4, 400}), 0.2);
    }

    // Quatre paires verticales refusées, toutes vers 26 % : le cas de 02-Philadelphia.
    TEST(OverlapCalibrationTest, AgreeingOutOfMarginPeaksGiveTheMedian) {
        const std::vector<OverlapObservation> observations{
            {Direction::Right, 0.209, false}, {Direction::Right, 0.21, false},
            {Direction::Bottom, 0.266, true}, {Direction::Bottom, 0.262, true},
            {Direction::Bottom, 0.26, true},  {Direction::Bottom, 0.262, true}};
        const auto vertical = StepsNs::calibrateOverlap(observations, Direction::Bottom);
        ASSERT_TRUE(vertical.has_value());
        EXPECT_NEAR(*vertical, 0.262, 1e-9);
        EXPECT_FALSE(StepsNs::calibrateOverlap(observations, Direction::Right).has_value());   // aucun pic hors marge
    }

    // Un pic hors marge isolé n'est jamais cru seul (motif répété possible).
    TEST(OverlapCalibrationTest, SingleOutOfMarginPeakIsNotTrusted) {
        EXPECT_FALSE(StepsNs::calibrateOverlap({{Direction::Bottom, 0.27, true}}, Direction::Bottom).has_value());
    }

    // Un pic hors marge en désaccord avec les paires mesurées de sa direction : la médiane reste au nominal, mais elle
    // n'est rendue que si une majorité s'accorde ; ici les paires mesurées l'emportent et le recalage vaut le nominal.
    TEST(OverlapCalibrationTest, OutlyingPeakDoesNotMoveTheMedian) {
        const std::vector<OverlapObservation> observations{
            {Direction::Bottom, 0.2, false}, {Direction::Bottom, 0.201, false},
            {Direction::Bottom, 0.199, false}, {Direction::Bottom, 0.32, true}};
        const auto vertical = StepsNs::calibrateOverlap(observations, Direction::Bottom);
        ASSERT_TRUE(vertical.has_value());
        EXPECT_NEAR(*vertical, 0.2005, 1e-9);
    }

    // Observations dispersées, sans majorité qui s'accorde : pas de recalage.
    TEST(OverlapCalibrationTest, ScatteredObservationsAreNotCalibrated) {
        const std::vector<OverlapObservation> observations{
            {Direction::Bottom, 0.2, false}, {Direction::Bottom, 0.27, true}, {Direction::Bottom, 0.34, true}};
        EXPECT_FALSE(StepsNs::calibrateOverlap(observations, Direction::Bottom).has_value());
    }

    // === phase-correlation ===

    StepsNs::PhaseCorrelationInput phaseInput() {
        StepsNs::PhaseCorrelationInput input;
        input.parameters.maximumPointCount = 0;   // translation seule : plus rapide, suffit ici
        return input;
    }

    TEST(OverlapCalibrationTest, PhaseCorrelationWithoutCalibrationRejectsVerticalPairs) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 2);
        auto input = phaseInput();
        input.overlapCalibration = false;
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::PhaseCorrelationStepClass step(input);
        step.setDataStore(data);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        EXPECT_EQ(step.getOutput().pairs.size(), 2u);
        ASSERT_EQ(step.getResult().at("failures").size(), 2u);
        for (const auto& failure : step.getResult().at("failures"))
            EXPECT_NE(failure.at("reason").get<std::string>().find("rail margin"), std::string::npos) << failure.dump();
        EXPECT_FALSE(step.getResult().contains("overlapCalibration"));
    }

    TEST(OverlapCalibrationTest, PhaseCorrelationCalibratesTheVerticalOverlap) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 2);
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::PhaseCorrelationStepClass step(phaseInput());
        step.setDataStore(data);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();

        const nlohmann::json result = step.getResult();   // getResult() renvoie une copie
        EXPECT_EQ(step.getOutput().pairs.size(), 4u) << result.dump();
        EXPECT_FALSE(result.contains("failures")) << result.dump();
        const auto* vertical = findCalibration(result, "Bottom");
        ASSERT_NE(vertical, nullptr) << result.dump();
        EXPECT_NEAR(vertical->at("calibrated").get<double>(), VERTICAL_OVERLAP, OVERLAP_TOLERANCE);
        EXPECT_EQ(vertical->at("recoveredPairs"), 2);
        EXPECT_EQ(findCalibration(result, "Right"), nullptr);

        for (const DataNs::PairMatches& pair : step.getOutput().pairs) {
            const cv::Point2d expected = pair.direction == Direction::Bottom ? cv::Point2d(0, VERTICAL_STEP)
                                                                             : cv::Point2d(HORIZONTAL_STEP, 0);
            EXPECT_NEAR(pair.featureMatches.translationX, expected.x, TRANSLATION_TOLERANCE);
            EXPECT_NEAR(pair.featureMatches.translationY, expected.y, TRANSLATION_TOLERANCE);
        }
        // L'ordre de parcours est gardé : tuile 0 (droite, dessous), tuile 1 (dessous), tuile 2 (droite)
        ASSERT_EQ(step.getOutput().pairs.size(), 4u);
        EXPECT_EQ(step.getOutput().pairs[0].tileIdB, 1);
        EXPECT_EQ(step.getOutput().pairs[1].tileIdB, 2);
        EXPECT_EQ(step.getOutput().pairs[2].tileIdB, 3);
        EXPECT_EQ(step.getOutput().pairs[3].tileIdA, 2);
    }

    // Le recouvrement vertical donné dans le script : rien à recaler.
    TEST(OverlapCalibrationTest, PhaseCorrelationUsesTheVerticalOverlapParameter) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 2);
        auto input = phaseInput();
        input.verticalOverlap = static_cast<float>(VERTICAL_OVERLAP);
        input.overlapCalibration = false;
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::PhaseCorrelationStepClass step(input);
        step.setDataStore(data);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        EXPECT_EQ(step.getOutput().pairs.size(), 4u) << step.getResult().dump();
    }

    // Une seule paire verticale : son pic hors marge n'est pas cru, le step échoue faute de paire.
    TEST(OverlapCalibrationTest, PhaseCorrelationDoesNotTrustASinglePair) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 1);
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::PhaseCorrelationStepClass step(phaseInput());
        step.setDataStore(data);
        EXPECT_FALSE(step.execute());
        EXPECT_FALSE(step.getResult().contains("overlapCalibration")) << step.getResult().dump();
        ASSERT_EQ(step.getResult().at("failures").size(), 1u);
    }

    TEST(OverlapCalibrationTest, PhaseCorrelationRejectsInvalidVerticalOverlap) {
        Directory directory;
        const auto tiles = makeGrid(directory, 1, 2);
        auto input = phaseInput();
        input.verticalOverlap = 1.2f;
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::PhaseCorrelationStepClass step(input);
        step.setDataStore(data);
        EXPECT_FALSE(step.execute());
        EXPECT_NE(step.getResult().at("error").get<std::string>().find("verticalOverlap"), std::string::npos);
    }

    // === overlap-estimation ===

    // Marge automatique de 10 % (100 px ici) : les paires verticales passent sans recalage.
    TEST(OverlapCalibrationTest, OverlapEstimationWideDefaultMarginAcceptsTheVerticalPairs) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 2);
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::OverlapEstimationStepClass step;
        step.setDataStore(data);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        EXPECT_EQ(step.getOutput().value.size(), 4u) << step.getResult().dump();
        EXPECT_FALSE(step.getResult().contains("overlapCalibration"));
        for (const auto& estimate : step.getOutput().value) {
            if (estimate.direction != Direction::Bottom) continue;
            EXPECT_NEAR(estimate.estimate.overlapFraction, VERTICAL_OVERLAP, OVERLAP_TOLERANCE);
        }
    }

    // Marge explicite de 50 px, trop étroite : le recalage récupère les deux paires verticales.
    TEST(OverlapCalibrationTest, OverlapEstimationCalibratesWithANarrowMargin) {
        Directory directory;
        const auto tiles = makeGrid(directory, 2, 2);
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);
        StepsNs::OverlapEstimationInput input;
        input.parameters.railMargin = 50.0;
        StepsNs::OverlapEstimationStepClass step(input);
        step.setDataStore(data);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        const nlohmann::json result = step.getResult();   // copie : findCalibration renvoie un pointeur dedans
        EXPECT_EQ(step.getOutput().value.size(), 4u) << result.dump();
        EXPECT_TRUE(result.at("failures").empty());
        const auto* vertical = findCalibration(result, "Bottom");
        ASSERT_NE(vertical, nullptr) << result.dump();
        EXPECT_NEAR(vertical->at("calibrated").get<double>(), VERTICAL_OVERLAP, OVERLAP_TOLERANCE);
        EXPECT_EQ(vertical->at("recoveredPairs"), 2);
    }

    // === sift ===

    TEST(OverlapCalibrationTest, SiftRejectsInvalidVerticalOverlapAndRailMargin) {
        Directory directory;
        const auto tiles = makeGrid(directory, 1, 2);
        StepNs::DataStoreClass data;
        data.replace("tiles", tiles);

        StepsNs::SiftInput badOverlap;
        badOverlap.verticalOverlap = 1.5f;
        StepsNs::SiftStepClass overlapStep(badOverlap);
        overlapStep.setDataStore(data);
        EXPECT_FALSE(overlapStep.execute());
        EXPECT_NE(overlapStep.getResult().at("error").get<std::string>().find("verticalOverlap"), std::string::npos);

        StepsNs::SiftInput badMargin;
        badMargin.railMargin = -1.0;
        StepsNs::SiftStepClass marginStep(badMargin);
        marginStep.setDataStore(data);
        EXPECT_FALSE(marginStep.execute());
        EXPECT_NE(marginStep.getResult().at("error").get<std::string>().find("railMargin"), std::string::npos);
    }

}
