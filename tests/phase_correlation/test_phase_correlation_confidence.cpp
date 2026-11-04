// tests/phase_correlation/test_phase_correlation_confidence.cpp
// Contrôles de confiance de PhaseCorrelationStepClass (voir docs/steps/PhaseCorrelationStep.md, § Confiance).
//
// GoogleTest, ctest -L unit : décision de checkConfidence sur des mesures construites, auto-similarité sur une texture
// et sur un motif répété, indicateurs en JSON, puis le step complet sur deux tuiles générées.

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/phase_correlation_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {

    using StepsNs::ConfidenceState;
    using StepsNs::PhaseCorrelationStepClass;

    constexpr int TILE_SIZE = 400;
    constexpr int STEP_TILE_WIDTH = 1000;      // tuiles du step : zone commune assez large pour des patchs
    constexpr int STEP_TILE_HEIGHT = 400;
    constexpr int STEP_TILE_SHIFT = 800;
    constexpr int TILE_SHIFT = 320;            // px : coin de B dans A
    constexpr int PATTERN_PERIOD = 30;         // px : période inférieure à la marge du rail
    constexpr int MAXIMUM_VALUE = 256;
    constexpr double TEXTURE_SIGMA = 2.0;
    constexpr int TEXTURE_SEED = 12345;
    constexpr int POINT_COUNT = 5;
    constexpr int POINT_SPACING = 20;
    constexpr float RELIABLE_SCORE = 0.9f;
    constexpr float POOR_SCORE = 0.1f;
    constexpr float LARGE_GAP = 2.0f;          // px : écart habituel sur de vraies tuiles, pour une mesure juste
    constexpr double GAP_TOLERANCE = 1e-3;
    constexpr double DISPERSION_LIMIT = 0.5;
    constexpr double REPEATED_LIMIT = 0.6;
    constexpr int SMALL_ZONE = 5;              // px : zone commune trop petite pour un patch
    constexpr int ROOMY_TRANSLATION = 100;     // px : la zone commune permet de placer des patchs
    constexpr double ROUNDED_VALUE = 0.12346;
    constexpr double EXPECTED_ROUNDED_VALUE = 0.123;

    cv::Mat createTexture(cv::Size size) {
        cv::Mat texture(size, CV_8UC1);
        cv::RNG random(TEXTURE_SEED);
        random.fill(texture, cv::RNG::UNIFORM, 0, MAXIMUM_VALUE);
        cv::GaussianBlur(texture, texture, {0, 0}, TEXTURE_SIGMA);
        return texture;
    }

    cv::Mat createRepeatedPattern() {
        const cv::Mat motif = createTexture({PATTERN_PERIOD, PATTERN_PERIOD});
        cv::Mat pattern;
        cv::repeat(motif, TILE_SIZE / PATTERN_PERIOD + 1, TILE_SIZE / PATTERN_PERIOD + 1, pattern);
        return pattern(cv::Rect(0, 0, TILE_SIZE, TILE_SIZE)).clone();
    }

    // Points dont pointsInA − pointsInB s'écarte de la translation de `gap` px.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) : aide de test, appels lisibles
    DataNs::Matches createMatches(float gap, float score) {
        DataNs::Matches matches;
        matches.translationX = static_cast<float>(TILE_SHIFT);
        for (int index = 0; index < POINT_COUNT; ++index) {
            const auto position = static_cast<float>(index * POINT_SPACING);
            matches.pointsInA.emplace_back(static_cast<float>(TILE_SHIFT) + position, position);
            matches.pointsInB.emplace_back(position - gap, position);
            matches.confidenceScores.push_back(score);
        }
        return matches;
    }

    class TemporaryDirectoryClass {

        public:
            TemporaryDirectoryClass() {
                const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                path = std::filesystem::temp_directory_path() / ("confidence-tests-" + std::to_string(stamp));
                std::filesystem::create_directory(path);
            }
            TemporaryDirectoryClass(const TemporaryDirectoryClass&) = delete;
            TemporaryDirectoryClass& operator=(const TemporaryDirectoryClass&) = delete;
            ~TemporaryDirectoryClass() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }

            std::filesystem::path path;

    };

    // Deux tuiles voisines découpées dans une même texture.
    DataNs::TileList createTiles(const std::filesystem::path& directory) {
        const cv::Mat source = createTexture({STEP_TILE_SHIFT + STEP_TILE_WIDTH, STEP_TILE_HEIGHT});
        const cv::Mat imageA = source(cv::Rect(0, 0, STEP_TILE_WIDTH, STEP_TILE_HEIGHT));
        const cv::Mat imageB = source(cv::Rect(STEP_TILE_SHIFT, 0, STEP_TILE_WIDTH, STEP_TILE_HEIGHT));
        const auto pathA = directory / "a.tif";
        const auto pathB = directory / "b.tif";
        cv::imwrite(pathA.string(), imageA);
        cv::imwrite(pathB.string(), imageB);
        auto tileA = std::make_shared<DataNs::TileClass>(0, pathA, "", imageA.cols, imageA.rows, imageA.type());
        auto tileB = std::make_shared<DataNs::TileClass>(1, pathB, "", imageB.cols, imageB.rows, imageB.type());
        tileA->setNeighbours(nullptr, tileB.get(), nullptr, nullptr);
        return {tileA, tileB};
    }

    TEST(PhaseCorrelationConfidence, DispersionIsReportedButDoesNotRejectByDefault) {
        const cv::Mat image = createTexture({TILE_SIZE, TILE_SIZE});
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(createMatches(LARGE_GAP, RELIABLE_SCORE), image, {});
        EXPECT_EQ(confidence.state, ConfidenceState::Reliable) << confidence.reason;
        ASSERT_TRUE(confidence.dispersion.has_value());
        EXPECT_NEAR(*confidence.dispersion, LARGE_GAP, GAP_TOLERANCE);
    }

    TEST(PhaseCorrelationConfidence, DispersionRejectsWhenEnabled) {
        const cv::Mat image = createTexture({TILE_SIZE, TILE_SIZE});
        StepsNs::ConfidenceContext context;
        context.control.dispersionMax = DISPERSION_LIMIT;
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(createMatches(LARGE_GAP, RELIABLE_SCORE), image, context);
        EXPECT_EQ(confidence.state, ConfidenceState::Rejected);
        EXPECT_NE(confidence.reason.find("dispersionMax"), std::string::npos);
    }

    TEST(PhaseCorrelationConfidence, PoorPatchesAreRejected) {
        const cv::Mat image = createTexture({TILE_SIZE, TILE_SIZE});
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(createMatches(0.f, POOR_SCORE), image, {});
        EXPECT_EQ(confidence.state, ConfidenceState::Rejected);
        EXPECT_NE(confidence.reason.find("nccMin"), std::string::npos);
    }

    TEST(PhaseCorrelationConfidence, RepeatedPatternIsAmbiguous) {
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(createMatches(0.f, RELIABLE_SCORE), createRepeatedPattern(), {});
        EXPECT_EQ(confidence.state, ConfidenceState::Ambiguous);
        ASSERT_TRUE(confidence.autosim.has_value());
        EXPECT_GE(*confidence.autosim, REPEATED_LIMIT);
    }

    TEST(PhaseCorrelationConfidence, TextureIsNotRepeated) {
        const double autosim = PhaseCorrelationStepClass::selfSimilarity(createTexture({TILE_SIZE, TILE_SIZE}), DataNs::Direction::Right,
                                                                         StepsNs::PhaseCorrelationInput{}.overlap, 0.0);
        EXPECT_LT(autosim, REPEATED_LIMIT);
    }

    TEST(PhaseCorrelationConfidence, NoPointInRoomyZoneIsRejected) {
        const cv::Mat image = createTexture({TILE_SIZE, TILE_SIZE});
        DataNs::Matches matches;
        matches.translationX = static_cast<float>(ROOMY_TRANSLATION);
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(matches, image, {});
        EXPECT_EQ(confidence.state, ConfidenceState::Rejected);
    }

    TEST(PhaseCorrelationConfidence, NoPointInSmallZoneIsKeptUnverified) {
        const cv::Mat image = createTexture({TILE_SIZE, TILE_SIZE});
        DataNs::Matches matches;
        matches.translationX = static_cast<float>(TILE_SIZE - SMALL_ZONE);
        const auto confidence = PhaseCorrelationStepClass::checkConfidence(matches, image, {});
        EXPECT_EQ(confidence.state, ConfidenceState::Unverified) << confidence.reason;
    }

    TEST(PhaseCorrelationConfidence, JsonUsesNullForMissingIndicators) {
        StepsNs::PairConfidence confidence;
        confidence.state = ConfidenceState::Rejected;
        confidence.ncc = ROUNDED_VALUE;
        const auto json = PhaseCorrelationStepClass::toJson(confidence);
        EXPECT_EQ(json.at("etat"), "rejete");
        EXPECT_DOUBLE_EQ(json.at("ncc").get<double>(), EXPECTED_ROUNDED_VALUE);
        EXPECT_TRUE(json.at("dispersion").is_null());
        EXPECT_TRUE(json.at("autosim").is_null());
    }

    TEST(PhaseCorrelationConfidence, StepKeepsReliablePairAndReportsIndicators) {
        const TemporaryDirectoryClass directory;
        StepNs::DataStoreClass dataStore;
        dataStore.replace("tiles", createTiles(directory.path));
        PhaseCorrelationStepClass step;
        step.setDataStore(dataStore);

        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        ASSERT_EQ(step.getOutput().pairs.size(), 1U);
        const auto result = step.getResult();
        ASSERT_EQ(result.at("confidence").size(), 1U);
        EXPECT_EQ(result.at("confidence").at(0).at("etat"), "fiable");
        EXPECT_FALSE(result.at("confidence").at(0).at("dispersion").is_null());
        EXPECT_FALSE(result.contains("failures"));
    }

    TEST(PhaseCorrelationConfidence, StepReportsRejectedPairInFailures) {
        const TemporaryDirectoryClass directory;
        StepNs::DataStoreClass dataStore;
        dataStore.replace("tiles", createTiles(directory.path));
        StepsNs::PhaseCorrelationInput input;
        input.confiance.autosimMax = -1.0;   // toute auto-similarité dépasse ce seuil
        PhaseCorrelationStepClass step(input);
        step.setDataStore(dataStore);

        EXPECT_FALSE(step.execute());
        EXPECT_TRUE(step.getOutput().pairs.empty());
        const auto result = step.getResult();
        ASSERT_EQ(result.at("failures").size(), 1U);
        EXPECT_EQ(result.at("failures").at(0).at("reason").get<std::string>().find("confiance :"), 0U);
        EXPECT_EQ(result.at("confidence").at(0).at("etat"), "ambigu");
    }

}
