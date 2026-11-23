// tests/frontend/test_pipeline_results.cpp
// Conversion des résultats du backend pour l'interface : paires (vue Alignement) et lignes de couture (vue Ligne de
// couture). Données construites à la main : aucune image n'est lue.
#include "pipeline_results.h"

#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/data/tile.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QObject>
#include <QRect>
#include <QSize>

#include <memory>
#include <vector>

namespace {

using FrontendUtils::PairStatus;

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr float SHIFT_X = 320.F;
constexpr int POINT_COUNT = 10;
constexpr double RESIDUAL = 1.25;

// Trois tuiles : 0 | 1 sur la première ligne, 2 sous la 0.
DataNs::TileList makeTiles() {
    DataNs::TileList tiles;
    for (int id = 0; id < 3; ++id)
        tiles.push_back(std::make_shared<DataNs::TileClass>(id, "photo_" + std::to_string(id) + ".tif",
                                                            "pyramide.tif", TILE_WIDTH, TILE_HEIGHT, CV_8UC3));
    tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, tiles[2].get());
    return tiles;
}

DataNs::PairMatches makePair(int idA, int idB, int pointCount) {
    DataNs::PairMatches pair{idA, idB, DataNs::Direction::Right, {}};
    for (int index = 0; index < pointCount; ++index) {
        const float y = 10.F * static_cast<float>(index);
        pair.featureMatches.pointsInA.emplace_back(SHIFT_X + 20.F, y);
        pair.featureMatches.pointsInB.emplace_back(20.F, y);
        pair.featureMatches.confidenceScores.push_back(1.F);
    }
    pair.featureMatches.translationX = SHIFT_X;
    return pair;
}

nlohmann::json phaseCorrelationResult(const char *state) {
    return {{"confidence", {{{"idA", 0}, {"idB", 1}, {"etat", state}, {"ncc", 0.9}}}},
            {"failures", {{{"idA", 0}, {"idB", 2}, {"reason", "tiles have different sizes"}}}}};
}

TEST(PipelineResultsTest, ReliablePairCarriesPointsSizesAndResidual) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)},
                                                  {phaseCorrelationResult("fiable")}, {{0, 1, {1.0, 0.75}, RESIDUAL}});
    ASSERT_EQ(pairs.size(), 2);
    const auto &pair = pairs.at(0);
    EXPECT_EQ(pair.status, PairStatus::Reliable);
    EXPECT_EQ(pair.tileA, QStringLiteral("photo_0"));
    EXPECT_EQ(pair.tileIndexA, 0);
    EXPECT_EQ(pair.tileIndexB, 1);
    EXPECT_EQ(pair.pointsInA.size(), POINT_COUNT);
    EXPECT_EQ(pair.tileSizeA, QSize(TILE_WIDTH, TILE_HEIGHT));
    EXPECT_EQ(pair.imagePathB, QStringLiteral("photo_1.tif"));
    EXPECT_DOUBLE_EQ(pair.dx, SHIFT_X);
    ASSERT_TRUE(pair.optimizationErrorPx.has_value());
    EXPECT_DOUBLE_EQ(*pair.optimizationErrorPx, RESIDUAL);
    ASSERT_TRUE(pair.nccScore.has_value());
    EXPECT_DOUBLE_EQ(*pair.nccScore, 0.9);
}

// Sans verdict ni résidu (SIFT, LightGlue, reprise) : NCC et écart restent « non mesurés », pas 1,00 et 0,00 px.
TEST(PipelineResultsTest, MissingNccAndResidualStayUnmeasured) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)}, {}, {});
    ASSERT_EQ(pairs.size(), 1);
    EXPECT_FALSE(pairs.at(0).nccScore.has_value());
    EXPECT_FALSE(pairs.at(0).optimizationErrorPx.has_value());
    EXPECT_TRUE(pairs.at(0).statusReason.contains(QStringLiteral("non mesuré"))) << "pas de corrélation annoncée";
    EXPECT_EQ(FrontendUtils::formatMeasure(pairs.at(0).optimizationErrorPx, 2, QStringLiteral(" px")),
              QObject::tr("non disponible"));
}

// Toutes les paires échouent : pas de `matches`, mais les raisons d'échec des steps donnent toutes les paires.
TEST(PipelineResultsTest, AllPairsFailedWithoutMatchesAreStillListed) {
    const nlohmann::json result = {{"failures", {{{"idA", 0}, {"idB", 1}, {"reason", "ambiguous correlation peak"}},
                                                 {{"idA", 0}, {"idB", 2}, {"reason", "tiles have different sizes"}}}}};
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {}, {result}, {});
    ASSERT_EQ(pairs.size(), 2);
    EXPECT_EQ(pairs.at(0).status, PairStatus::Failed);
    EXPECT_EQ(pairs.at(1).status, PairStatus::Failed);
    EXPECT_TRUE(pairs.at(0).statusReason.contains(QStringLiteral("ambiguous")));
}

// Une paire corrigée par manual-matches est validée, même si l'appariement l'avait jugée ambiguë.
TEST(PipelineResultsTest, ManuallyCorrectedPairIsValidated) {
    const nlohmann::json manualResult = {{"correctedPairs", {{{"idA", 0}, {"idB", 1}, {"added", false}}}}};
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, 1)},
                                                  {phaseCorrelationResult("ambigu"), manualResult}, {});
    EXPECT_EQ(pairs.at(0).status, PairStatus::Validated);
}

// Le point de la correction tombe au centre de la zone commune, et sa différence redonne le décalage saisi.
TEST(PipelineResultsTest, ManualCorrectionReproducesTheOffset) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)}, {}, {});
    const FrontendNs::ManualCorrection correction = FrontendNs::manualCorrectionFor(pairs.at(0), 330.0, -6.0);
    EXPECT_EQ(correction.tileIdA, 0);
    EXPECT_EQ(correction.tileIdB, 1);
    ASSERT_EQ(correction.pointsInA.size(), 1U);
    ASSERT_EQ(correction.pointsInB.size(), 1U);
    EXPECT_DOUBLE_EQ(correction.pointsInA[0][0] - correction.pointsInB[0][0], 330.0);
    EXPECT_DOUBLE_EQ(correction.pointsInA[0][1] - correction.pointsInB[0][1], -6.0);
    EXPECT_DOUBLE_EQ(correction.pointsInA[0][0], (330.0 + TILE_WIDTH) / 2.0);   // milieu de x = 330..400
}

// Une nouvelle correction remplace celle de la même paire, saisie dans un sens ou dans l'autre.
TEST(PipelineResultsTest, NewCorrectionReplacesTheSamePair) {
    const FrontendNs::ManualCorrection first{0, 1, {{1.0, 1.0}}, {{0.0, 0.0}}};
    const FrontendNs::ManualCorrection other{0, 2, {{1.0, 1.0}}, {{0.0, 0.0}}};
    const FrontendNs::ManualCorrection reversed{1, 0, {{5.0, 5.0}}, {{0.0, 0.0}}};
    const auto corrections = FrontendNs::withCorrection({first, other}, reversed);
    ASSERT_EQ(corrections.size(), 2U);
    EXPECT_EQ(corrections.at(0), other);
    EXPECT_EQ(corrections.at(1), reversed);
}

// Une paire citée dans `failures` mais absente de `matches` apparaît en échec, avec la raison du backend.
TEST(PipelineResultsTest, FailedPairIsListedWithItsReason) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)},
                                                  {phaseCorrelationResult("fiable")}, {});
    ASSERT_EQ(pairs.size(), 2);
    const auto &failed = pairs.at(1);
    EXPECT_EQ(failed.pairIndex, 1);
    EXPECT_EQ(failed.status, PairStatus::Failed);
    EXPECT_TRUE(failed.statusReason.contains(QStringLiteral("different sizes")));
    EXPECT_EQ(failed.direction, QObject::tr("vertical"));
    EXPECT_TRUE(failed.pointsInA.isEmpty());
}

TEST(PipelineResultsTest, AmbiguousVerdictMakesThePairDubious) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)},
                                                  {phaseCorrelationResult("ambigu")}, {});
    EXPECT_EQ(pairs.at(0).status, PairStatus::Dubious);
}

// Sans verdict (step repris d'un checkpoint, SIFT…), peu de points rend la paire douteuse, pas en échec : elle a servi
// au positionnement.
TEST(PipelineResultsTest, FewPointsWithoutVerdictIsDubiousNotFailed) {
    const auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, 2)}, {}, {});
    ASSERT_EQ(pairs.size(), 1);
    EXPECT_EQ(pairs.at(0).status, PairStatus::Dubious);
}

// Deux territoires côte à côte (0 : x < 100, 1 : x >= 100), aperçu réduit de moitié : la couture tombe vers x = 50.
TEST(PipelineResultsTest, SeamLinesFollowOnlyTheBoundaryBetweenTiles) {
    const std::vector<DataNs::TileTerritory> territories = {
        {0, {{{0, 0}, {99, 0}, {99, 99}, {0, 99}}}, {{}}},
        {1, {{{100, 0}, {199, 0}, {199, 99}, {100, 99}}}, {{}}}};
    const SeamLines seams = FrontendNs::buildSeamLines(territories, QSize(100, 50), 0.5);
    ASSERT_FALSE(seams.isEmpty());
    EXPECT_EQ(seams.frame, QSize(100, 50));
    QRectF extent;
    for (const QPolygonF &line : seams.lines) {
        extent |= line.boundingRect();
        for (const QPointF &point : line)
            EXPECT_NEAR(point.x(), 50.0, 1.0) << "ni à l'intérieur d'une tuile, ni au bord extérieur de la mosaïque";
    }
    EXPECT_LT(extent.top(), 2.0) << "la couture traverse toute la hauteur";
    EXPECT_GT(extent.bottom(), 47.0);
}

// Toutes les tuiles, avec leur case de grille : la vue Alignement les place toutes.
TEST(PipelineResultsTest, ResultTilesKeepIdsSizesAndGridCells) {
    DataNs::TileList tiles = makeTiles();
    tiles[2]->setGridPosition(1, 0);
    const auto resultTiles = FrontendNs::buildResultTiles(tiles);
    ASSERT_EQ(resultTiles.size(), 3);
    EXPECT_EQ(resultTiles.at(2).id, 2);
    EXPECT_EQ(resultTiles.at(2).row, 1);
    EXPECT_EQ(resultTiles.at(2).column, 0);
    EXPECT_EQ(resultTiles.at(2).size, QSize(TILE_WIDTH, TILE_HEIGHT));
    EXPECT_EQ(resultTiles.at(2).name, QStringLiteral("photo_2"));
}

// La zone estimée par overlap-estimation est recopiée dans la paire, même en échec : on voit où elle a été cherchée.
TEST(PipelineResultsTest, OverlapEstimateIsAttachedToItsPair) {
    auto pairs = FrontendNs::buildPairInfos(makeTiles(), {makePair(0, 1, POINT_COUNT)},
                                            {phaseCorrelationResult("fiable")}, {});
    DataNs::PairOverlapEstimate estimate;
    estimate.tileIdA = 0;
    estimate.tileIdB = 2;
    estimate.commonAreaInA = cv::Rect(0, 240, TILE_WIDTH, 60);
    estimate.commonAreaInB = cv::Rect(0, 0, TILE_WIDTH, 60);
    estimate.estimate.translationBInA = cv::Point2d(0.0, 240.0);
    FrontendNs::attachOverlapEstimates(pairs, {estimate});
    EXPECT_TRUE(pairs.at(0).overlapInA.isNull());
    EXPECT_EQ(pairs.at(1).overlapInA, QRect(0, 240, TILE_WIDTH, 60));
    EXPECT_EQ(pairs.at(1).overlapInB, QRect(0, 0, TILE_WIDTH, 60));
    EXPECT_DOUBLE_EQ(pairs.at(1).dy, 240.0) << "point de départ de la correction manuelle d'une paire en échec";
}

TEST(PipelineResultsTest, NoTerritoryGivesNoOverlay) {
    EXPECT_TRUE(FrontendNs::buildSeamLines({}, QSize(100, 50), 0.5).isEmpty());
}

}   // namespace
