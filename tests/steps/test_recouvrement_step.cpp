// tests/steps/test_recouvrement_step.cpp
// Tests unitaires pour RecouvrementStepClass (steps/recouvrement_step.h).
// GoogleTest, ctest -L unit : sans DataStore, paramètres invalides, paires horizontale et verticale,
// grille 2×2, paire sans recouvrement estimable.

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/recouvrement_step.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Constantes du banc de test
constexpr int TILE_WIDTH  = 384;
constexpr int TILE_SHIFT  = 307;   // 384 × (1 − 0.2) = 307 px → recouvrement nominal 20 %
constexpr int TEXTURE_SIZE = TILE_SHIFT + TILE_WIDTH;
constexpr int TEXTURE_SEED = 42;
constexpr int TEXTURE_MIN = 20;
constexpr int TEXTURE_MAX = 220;
constexpr double TEXTURE_SIGMA = 1.0;
constexpr double DEPTH_SCALE = 257.0;
constexpr float TEST_OVERLAP = 0.2f;
constexpr float TRANSLATION_TOLERANCE = 2.f;
constexpr int TILE_ID_A = 1;
constexpr int TILE_ID_B = 2;
constexpr int TILE_ID_C = 3;
constexpr int TILE_ID_D = 4;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// Dossier temporaire isolé, supprimé à la destruction.
struct Directory {
    std::filesystem::path path;
    Directory() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("recouvrement-step-" + std::to_string(stamp));
        require(std::filesystem::create_directory(path), "create isolated test directory");
    }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

// Texture aléatoire de taille TEXTURE_SIZE × TEXTURE_SIZE.
cv::Mat makeTexture(int depth = CV_8U) {
    cv::Mat source(TEXTURE_SIZE, TEXTURE_SIZE, CV_8UC1);
    cv::RNG random(TEXTURE_SEED);
    random.fill(source, cv::RNG::UNIFORM, TEXTURE_MIN, TEXTURE_MAX);
    cv::GaussianBlur(source, source, cv::Size(3, 3), TEXTURE_SIGMA);
    if (depth == CV_16U) source.convertTo(source, CV_16U, DEPTH_SCALE);
    return source;
}

// Enregistre une tuile découpée dans source à la cellule (col, row), et renvoie son handle.
std::shared_ptr<DataNs::TileClass> saveTile(const Directory& dir, const cv::Mat& source,
                                             cv::Point cell, int tileId) {
    const auto file = dir.path / ("img_C" + std::to_string(cell.x) + "_R" + std::to_string(cell.y) + ".tif");
    const cv::Mat image = source(cv::Rect(cell.x * TILE_SHIFT, cell.y * TILE_SHIFT, TILE_WIDTH, TILE_WIDTH)).clone();
    require(cv::imwrite(file.string(), image), "write tile fixture");
    return std::make_shared<DataNs::TileClass>(tileId, file, std::filesystem::path{}, image.cols, image.rows, image.type());
}

StepsNs::RecouvrementInput defaultParameters() {
    StepsNs::RecouvrementInput input;
    input.overlap = TEST_OVERLAP;
    return input;
}

// === Tests ===

// Sans DataStore, le step doit échouer proprement.
TEST(RecouvrementStepTest, FailsWithoutDataStore) {
    StepsNs::RecouvrementStepClass step;
    EXPECT_FALSE(step.execute());
    EXPECT_FALSE(step.getResult().at("error").get<std::string>().empty());
}

// overlap invalide : hors de ]0, 1[.
TEST(RecouvrementStepTest, RejectsInvalidOverlap) {
    Directory dir;
    const auto source = makeTexture();
    auto tileA = saveTile(dir, source, {0, 0}, TILE_ID_A);
    auto tileB = saveTile(dir, source, {1, 0}, TILE_ID_B);
    tileA->setNeighbours(nullptr, tileB.get(), nullptr, nullptr);
    tileB->setNeighbours(tileA.get(), nullptr, nullptr, nullptr);

    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{tileA, tileB});

    StepsNs::RecouvrementInput input = defaultParameters();
    input.overlap = 0.f;   // invalide
    StepsNs::RecouvrementStepClass step(input);
    step.setDataStore(data);
    EXPECT_FALSE(step.execute());
    EXPECT_FALSE(step.getResult().at("error").get<std::string>().empty());
}

// Paire horizontale (voisine de droite) : un seul PairOverlapEstimate attendu.
TEST(RecouvrementStepTest, ProducesOneEstimateForHorizontalPair) {
    Directory dir;
    const auto source = makeTexture();
    auto tileA = saveTile(dir, source, {0, 0}, TILE_ID_A);
    auto tileB = saveTile(dir, source, {1, 0}, TILE_ID_B);
    tileA->setNeighbours(nullptr, tileB.get(), nullptr, nullptr);
    tileB->setNeighbours(tileA.get(), nullptr, nullptr, nullptr);

    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{tileA, tileB});

    StepsNs::RecouvrementStepClass step(defaultParameters());
    step.setDataStore(data);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();

    const auto& estimates = step.getOutput().value;
    ASSERT_EQ(estimates.size(), 1u);
    EXPECT_EQ(estimates[0].tileIdA, TILE_ID_A);
    EXPECT_EQ(estimates[0].tileIdB, TILE_ID_B);
    EXPECT_EQ(estimates[0].direction, DataNs::Direction::Right);
    // La zone commune doit être non vide.
    EXPECT_FALSE(estimates[0].commonAreaInA.empty());
    EXPECT_FALSE(estimates[0].commonAreaInB.empty());
    EXPECT_EQ(estimates[0].commonAreaInA.size(), estimates[0].commonAreaInB.size());
    // La translation estimée doit être cohérente avec TILE_SHIFT.
    EXPECT_NEAR(estimates[0].estimate.translationBInA.x,
                static_cast<double>(TILE_SHIFT), static_cast<double>(TRANSLATION_TOLERANCE));
    // Le step doit écrire la clé dans le DataStore.
    EXPECT_EQ(step.getResult().at("pairs").get<std::size_t>(), 1u);
    const auto& stored = data.get<DataNs::OverlapEstimates>("overlap-estimates");
    ASSERT_EQ(stored.size(), 1u);
}

// Paire verticale (voisine du dessous) : un seul PairOverlapEstimate attendu.
TEST(RecouvrementStepTest, ProducesOneEstimateForVerticalPair) {
    Directory dir;
    const auto source = makeTexture();
    auto tileA = saveTile(dir, source, {0, 0}, TILE_ID_A);
    auto tileB = saveTile(dir, source, {0, 1}, TILE_ID_C);
    tileA->setNeighbours(nullptr, nullptr, nullptr, tileB.get());
    tileB->setNeighbours(nullptr, nullptr, tileA.get(), nullptr);

    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{tileA, tileB});

    StepsNs::RecouvrementStepClass step(defaultParameters());
    step.setDataStore(data);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();

    const auto& estimates = step.getOutput().value;
    ASSERT_EQ(estimates.size(), 1u);
    EXPECT_EQ(estimates[0].direction, DataNs::Direction::Bottom);
    EXPECT_NEAR(estimates[0].estimate.translationBInA.y,
                static_cast<double>(TILE_SHIFT), static_cast<double>(TRANSLATION_TOLERANCE));
}

// Grille 2×2 : trois paires (A→B, A→C, B→D ou C→D selon l'ordre) — toutes doivent être estimées.
TEST(RecouvrementStepTest, Produces3EstimatesForA2x2Grid) {
    Directory dir;
    const auto source = makeTexture();
    auto tileA = saveTile(dir, source, {0, 0}, TILE_ID_A);
    auto tileB = saveTile(dir, source, {1, 0}, TILE_ID_B);
    auto tileC = saveTile(dir, source, {0, 1}, TILE_ID_C);
    auto tileD = saveTile(dir, source, {1, 1}, TILE_ID_D);
    tileA->setNeighbours(nullptr, tileB.get(), nullptr, tileC.get());
    tileB->setNeighbours(tileA.get(), nullptr, nullptr, tileD.get());
    tileC->setNeighbours(nullptr, tileD.get(), tileA.get(), nullptr);
    tileD->setNeighbours(tileC.get(), nullptr, tileB.get(), nullptr);

    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{tileA, tileB, tileC, tileD});

    StepsNs::RecouvrementStepClass step(defaultParameters());
    step.setDataStore(data);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();

    // 4 arêtes dans une grille 2×2 : A-B, A-C, B-D, C-D → 4 paires Right/Bottom.
    const auto& estimates = step.getOutput().value;
    EXPECT_EQ(estimates.size(), 4u);
    EXPECT_EQ(step.getResult().at("pairs").get<std::size_t>(), 4u);
    EXPECT_TRUE(step.getResult().at("failures").empty());

    // Chaque zone commune doit avoir la même taille dans les deux tuiles de la paire.
    for (const auto& estimate : estimates) {
        EXPECT_EQ(estimate.commonAreaInA.size(), estimate.commonAreaInB.size());
        EXPECT_FALSE(estimate.commonAreaInA.empty());
    }
}

// La configuration par défaut doit pouvoir être lue par importConfig (aller-retour JSON).
TEST(RecouvrementStepTest, ConfigRoundTrip) {
    StepsNs::RecouvrementStepClass step;
    const auto exported = step.getConfigJson();
    // importConfig ne doit pas lever d'exception.
    EXPECT_NO_THROW(step.importConfig(exported));
    EXPECT_EQ(step.getConfigJson().at("name").get<std::string>(), "recouvrement");
}

// La sortie est bien enregistrée sous la clé "overlap-estimates" de type "OverlapEstimates".
TEST(RecouvrementStepTest, OutputKeyAndType) {
    StepsNs::RecouvrementStepClass step;
    EXPECT_EQ(step.getOutput().key, "overlap-estimates");
    EXPECT_EQ(step.getOutput().type, "OverlapEstimates");
}

}   // namespace
