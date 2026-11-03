// src/backend/algorithms/stitching/positioning/tile_positioner.cpp
#include "algorithms/stitching/positioning/tile_positioner.h"

#include "algorithms/rejection/outlier_rejector.h"
#include "algorithms/rejection/ransac/ransac_rejector.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "algorithms/stitching/feature_matcher.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "algorithms/stitching/sift/sift_matcher.h"
#ifdef STITCHING_PROJECT_ROOT
#include "algorithms/stitching/lightglue/lightglue_matcher.h"
#endif

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <memory>
#include <ostream>
#include <utility>

namespace PositioningNs {

    using DataNs::Direction;

    namespace {

        struct TileInfo {
            std::string filePath;
            std::string tileId;
            int row = 0, column = 0;
        };

        // Décalage mesuré entre deux tuiles voisines : positionB - positionA
        struct TileOffset {
            std::size_t tileIndexA, tileIndexB;
            cv::Point2d positionDifference;
            double rotationRadians = 0.0;   // angle de rotation relatif mesuré entre la tuile B et la tuile A
        };

        // Médiane ; pour un nombre pair de valeurs, moyenne des deux valeurs du milieu.
        double computeMedian(std::vector<double> values) {
            const std::size_t valueCount = values.size();
            const auto middle = values.begin() + valueCount / 2;
            std::nth_element(values.begin(), middle, values.end());
            double medianValue = *middle;
            if (valueCount % 2 == 0) medianValue = 0.5 * (medianValue + *std::max_element(values.begin(), middle));
            return medianValue;
        }

        // Moindres carrés : positionB - positionA = positionDifference pour toutes les paires. Tuile 0 = référence (0, 0).
        // Seules les tuiles reliées à la référence sont résolues (connectedTiles[i] = true).
        // Les angles de rotation absolus de chaque tuile (solvedRotations) sont résolus en même temps, par SVD.
        bool solvePositions(std::size_t tileCount, const std::vector<TileOffset>& offsets, std::vector<bool>& connectedTiles,
                            std::vector<cv::Point2d>& solvedPositions, std::vector<double>& solvedRotations) {
            connectedTiles.assign(tileCount, false);
            connectedTiles[0] = true;
            for (bool hasChanged = true; hasChanged;) {
                hasChanged = false;
                for (const TileOffset& tileOffset : offsets) {
                    if (connectedTiles[tileOffset.tileIndexA] && !connectedTiles[tileOffset.tileIndexB]) {
                        connectedTiles[tileOffset.tileIndexB] = true;
                        hasChanged = true;
                    } else if (connectedTiles[tileOffset.tileIndexB] && !connectedTiles[tileOffset.tileIndexA]) {
                        connectedTiles[tileOffset.tileIndexA] = true;
                        hasChanged = true;
                    }
                }
            }

            std::vector<int> variableIndex(tileCount, -1);
            int variableCount = 0;
            for (std::size_t tileIndex = 1; tileIndex < tileCount; ++tileIndex)
                if (connectedTiles[tileIndex]) variableIndex[tileIndex] = variableCount++;

            std::vector<const TileOffset*> usableOffsets;
            for (const TileOffset& tileOffset : offsets)
                if (connectedTiles[tileOffset.tileIndexA] && connectedTiles[tileOffset.tileIndexB])
                    usableOffsets.push_back(&tileOffset);

            solvedPositions.assign(tileCount, cv::Point2d(0.0, 0.0));
            solvedRotations.assign(tileCount, 0.0);   // angles de rotation initialisés à 0.0 rad
            if (variableCount == 0) return true;

            const int equationCount = static_cast<int>(usableOffsets.size());
            cv::Mat coefficients(equationCount, variableCount, CV_64F, cv::Scalar(0));
            // rightHandSideRotation : second membre pour la résolution des angles de rotation
            cv::Mat rightHandSideX(equationCount, 1, CV_64F), rightHandSideY(equationCount, 1, CV_64F),
                rightHandSideRotation(equationCount, 1, CV_64F);
            for (int equationIndex = 0; equationIndex < equationCount; ++equationIndex) {
                const TileOffset& tileOffset = *usableOffsets[equationIndex];
                if (tileOffset.tileIndexB != 0) coefficients.at<double>(equationIndex, variableIndex[tileOffset.tileIndexB]) += 1.0;
                if (tileOffset.tileIndexA != 0) coefficients.at<double>(equationIndex, variableIndex[tileOffset.tileIndexA]) -= 1.0;
                rightHandSideX.at<double>(equationIndex, 0) = tileOffset.positionDifference.x;
                rightHandSideY.at<double>(equationIndex, 0) = tileOffset.positionDifference.y;
                // Équation theta_B - theta_A = rotation_AB, résolue sur le même graphe
                rightHandSideRotation.at<double>(equationIndex, 0) = tileOffset.rotationRadians;
            }

            // Résolution simultanée des positions X, Y et des angles de rotation par SVD
            cv::Mat solutionX, solutionY, solutionRotation;
            if (!cv::solve(coefficients, rightHandSideX, solutionX, cv::DECOMP_SVD) ||
                !cv::solve(coefficients, rightHandSideY, solutionY, cv::DECOMP_SVD) ||
                !cv::solve(coefficients, rightHandSideRotation, solutionRotation, cv::DECOMP_SVD))
                return false;
            for (std::size_t tileIndex = 1; tileIndex < tileCount; ++tileIndex)
                if (connectedTiles[tileIndex]) {
                    solvedPositions[tileIndex] = cv::Point2d(solutionX.at<double>(variableIndex[tileIndex], 0),
                                                             solutionY.at<double>(variableIndex[tileIndex], 0));
                    solvedRotations[tileIndex] = solutionRotation.at<double>(variableIndex[tileIndex], 0);
                }
            return true;
        }

    }

    bool computeTilePositions(const std::vector<std::string>& tilePaths, const PositioningSettings& settings,
                              std::vector<DataNs::TilePositionData>& positions, std::ostream& log) {
        positions.clear();
        using GraphCutNs::ImagePretreatmentClass;

        // Tuiles : nom -> (ligne, colonne, identifiant)
        std::vector<TileInfo> tiles;
        for (const std::string& tilePath : tilePaths) {
            TileInfo tile;
            tile.filePath = tilePath;
            if (!ImagePretreatmentClass::parseTileName(tilePath, tile.row, tile.column, tile.tileId)) {
                log << "wrong tile name: " << tilePath << '\n';
                return false;
            }
            tiles.push_back(std::move(tile));
        }
        if (tiles.empty()) { log << "No tiles\n"; return false; }

        std::sort(tiles.begin(), tiles.end(), [](const TileInfo& first, const TileInfo& second) {
            return first.row != second.row ? first.row < second.row : first.column < second.column;
        });
        std::map<std::pair<int, int>, std::size_t> tileIndexByGridPosition;
        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
            if (!tileIndexByGridPosition.emplace(std::make_pair(tiles[tileIndex].row, tiles[tileIndex].column), tileIndex).second) {
                log << "Duplicate tile grid position " << tiles[tileIndex].tileId << '\n';
                return false;
            }

        if (tiles.size() == 1) {
            positions.push_back({tiles[0].tileId, cv::Point2d(0.0, 0.0), 0.0});
            return true;
        }

        // Paires de voisins directs : première passe OverlapEstimatorClass -> matcher -> rejector.
        // Sélection polymorphe du matcher et du rejector :
        // - matchers autorisés : "sift", "correlation_phase", "lightglue" (si STITCHING_PROJECT_ROOT est défini)
        // - rejectors autorisés : "vote" (par défaut, Hough 2D) et "ransac" (estimateAffinePartial2D)
        std::unique_ptr<StitchingNs::FeatureMatcherClass> matcher;
        if (settings.matcherName == "sift") matcher = std::make_unique<SiftNs::SiftMatcherClass>(settings.matcherParameters);
        else if (settings.matcherName == "correlation_phase") matcher = std::make_unique<PhaseCorrelationNs::PhaseCorrelationMatcherClass>();
#ifdef STITCHING_PROJECT_ROOT
        else if (settings.matcherName == "lightglue") matcher = std::make_unique<LightGlueNs::LightGlueMatcherClass>();
#endif
        std::unique_ptr<RejectionNs::OutlierRejectorClass> rejector;
        if (settings.rejectorName == "ransac") rejector = std::make_unique<RansacNs::RansacRejectorClass>(settings.rejectorParameters);
        else if (settings.rejectorName == "vote") rejector = std::make_unique<RansacNs::VoteRejectorClass>(settings.rejectorParameters);
        if (!matcher || !rejector) {
            log << "Matcher or rejector not available (" << settings.matcherName << " / " << settings.rejectorName << ")\n";
            return false;
        }
        OverlapEstimationNs::OverlapEstimatorClass overlapEstimator;

        std::vector<TileOffset> offsets;
        std::size_t attemptedPairCount = 0;
        const auto processPair = [&](std::size_t tileIndexA, std::size_t tileIndexB, Direction direction) {
            ++attemptedPairCount;
            log << tiles[tileIndexA].tileId << " -> " << tiles[tileIndexB].tileId
                << (direction == Direction::Right ? " (H) : " : " (V) : ");

            // 16 bits sans perte, 2 tuiles à la fois
            const cv::Mat imageA = ImagePretreatmentClass::read16Bit(tiles[tileIndexA].filePath);
            const cv::Mat imageB = ImagePretreatmentClass::read16Bit(tiles[tileIndexB].filePath);
            if (imageA.empty() || imageB.empty()) {
                log << "cannot read (" << (imageA.empty() ? tiles[tileIndexA].filePath : tiles[tileIndexB].filePath) << ")\n";
                return false;
            }

            // Première passe : estimation rapide du recouvrement réel au pixel près
            const auto overlapEstimate = overlapEstimator.estimate(imageA, imageB, {direction, settings.overlapFraction});
            if (!overlapEstimate) { log << overlapEstimator.getFailureReason() << ", pair ignored\n"; return true; }
            const DataNs::PairContext context{direction, overlapEstimate->overlapFraction, overlapEstimate->translationBInA};

            DataNs::Matches matches;
            if (settings.matcherName == "correlation_phase") {
                if (!matcher->match(imageA, imageB, context, matches)) {
                    log << "phase correlation: failed, pair ignored\n";
                    return true;
                }
            } else {
                // Découpe des seules bandes de chevauchement effectif (areaA dans A, areaB dans B) au lieu d'envoyer les
                // tuiles entières, puis translation des points détectés par areaA.tl() et areaB.tl() pour les réexprimer
                // dans le repère global des tuiles entières avant le rejet des aberrants.
                const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                    imageA.size(), imageB.size(), overlapEstimate->translationBInA);
                if (areaA.empty() || !matcher->match(imageA(areaA), imageB(areaB), context, matches)) {
                    log << settings.matcherName << ": not enough matches, pair ignored\n";
                    return true;
                }
                for (auto& point : matches.pointsInA) point += cv::Point2f(areaA.tl());
                for (auto& point : matches.pointsInB) point += cv::Point2f(areaB.tl());
            }

            DataNs::Alignment alignment;
            if (!rejector->estimate(matches, alignment) || alignment.inlierMask.size() != matches.pointsInA.size()) {
                log << matches.pointsInA.size() << " matches, RANSAC failed, pair ignored\n";
                return true;
            }

            std::vector<double> translationsX, translationsY;
            for (std::size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
                if (!alignment.inlierMask[pointIndex]) continue;
                translationsX.push_back(static_cast<double>(matches.pointsInA[pointIndex].x) - matches.pointsInB[pointIndex].x);
                translationsY.push_back(static_cast<double>(matches.pointsInA[pointIndex].y) - matches.pointsInB[pointIndex].y);
            }
            if (translationsX.empty()) { log << "no inlier, pair ignored\n"; return true; }

            const cv::Point2d positionDifference(computeMedian(translationsX), computeMedian(translationsY));
            // Angle relatif theta = atan2(m10, m00), extrait de la matrice 3x3 d'alignement
            const double rotationRadians = alignment.transformBToA.empty() ? 0.0
                : std::atan2(alignment.transformBToA.at<double>(1, 0), alignment.transformBToA.at<double>(0, 0));
            offsets.push_back({tileIndexA, tileIndexB, positionDifference, rotationRadians});
            log << matches.pointsInA.size() << " matches, " << translationsX.size() << " inliers, offset = ("
                << std::fixed << std::setprecision(2) << positionDifference.x << ", " << positionDifference.y << "), rot = "
                << std::setprecision(4) << rotationRadians << " rad\n";
            return true;
        };

        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
            const auto rightNeighbour = tileIndexByGridPosition.find({tiles[tileIndex].row, tiles[tileIndex].column + 1});
            const auto bottomNeighbour = tileIndexByGridPosition.find({tiles[tileIndex].row + 1, tiles[tileIndex].column});
            if (rightNeighbour != tileIndexByGridPosition.end() && !processPair(tileIndex, rightNeighbour->second, Direction::Right))
                return false;
            if (bottomNeighbour != tileIndexByGridPosition.end() && !processPair(tileIndex, bottomNeighbour->second, Direction::Bottom))
                return false;
        }

        if (attemptedPairCount == 0) { log << "No horizontally or vertically adjacent tile pairs were found\n"; return false; }
        if (offsets.empty()) { log << "SIFT + RANSAC did not give a usable offset for any neighboring pair\n"; return false; }

        // Ajustement global
        std::vector<bool> connectedTiles;
        std::vector<cv::Point2d> solvedPositions;
        std::vector<double> solvedRotations;   // angle de rotation global calculé pour chaque tuile
        if (!solvePositions(tiles.size(), offsets, connectedTiles, solvedPositions, solvedRotations)) {
            log << "Could not solve tile positions\n";
            return false;
        }

        // Résidu par paire : écart entre l'offset mesuré et celui de la solution globale (contrôle de cohérence)
        log << "Adjustment residuals (measured offset - adjusted offset):\n";
        for (const TileOffset& tileOffset : offsets) {
            if (!connectedTiles[tileOffset.tileIndexA] || !connectedTiles[tileOffset.tileIndexB]) continue;
            const cv::Point2d residual = tileOffset.positionDifference -
                                         (solvedPositions[tileOffset.tileIndexB] - solvedPositions[tileOffset.tileIndexA]);
            log << "  " << tiles[tileOffset.tileIndexA].tileId << " -> " << tiles[tileOffset.tileIndexB].tileId << " : ("
                << std::fixed << std::setprecision(2) << residual.x << ", " << residual.y << ")\n";
        }

        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
            // Solution globale, angle de rotation compris
            if (connectedTiles[tileIndex]) positions.push_back({tiles[tileIndex].tileId, solvedPositions[tileIndex], solvedRotations[tileIndex]});
            else log << "WARNING: tile " << tiles[tileIndex].tileId << " is not linked to the reference tile by any valid pair\n";
        }
        return !positions.empty();
    }

}
