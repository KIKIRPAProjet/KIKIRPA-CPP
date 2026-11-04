// src/backend/algorithms/stitching/positioning/tile_positioner.cpp
#include "algorithms/stitching/positioning/tile_positioner.h"

#include "algorithms/rejection/outlier_rejector.h"
#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "algorithms/stitching/feature_matcher.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "algorithms/stitching/sift/sift_matcher.h"
#include "core/data/algorithm_types.h"
#ifdef STITCHING_PROJECT_ROOT
#include "algorithms/stitching/lightglue/lightglue_matcher.h"
#endif

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace PositioningNs {

    using DataNs::Direction;

    namespace {

        constexpr double AVERAGE_OF_TWO_WEIGHT = 0.5;

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

        // Solution globale. Seules les tuiles reliées à la référence sont résolues (connectedTiles[i] = true).
        struct SolvedTiles {
            std::vector<bool> connectedTiles;
            std::vector<cv::Point2d> positions;
            std::vector<double> rotations;   // angle de rotation global calculé pour chaque tuile
        };

        // Système positionB - positionA = positionDifference, avec un second membre par inconnue résolue.
        struct LinearSystem {
            cv::Mat coefficients;
            cv::Mat rightHandSideX;
            cv::Mat rightHandSideY;
            cv::Mat rightHandSideRotation;
        };

        using GridIndex = std::map<std::pair<int, int>, std::size_t>;

        // Tuiles triées et leur indice par position (ligne, colonne) dans la grille.
        struct TileGrid {
            std::vector<TileInfo> tiles;
            GridIndex index;
        };

        // Paire de voisins directs à mesurer.
        struct TilePair {
            std::size_t tileIndexA;
            std::size_t tileIndexB;
            Direction direction;
        };

        // Les deux images d'une paire, en 16 bits sans perte.
        struct PairImages {
            cv::Mat imageA;
            cv::Mat imageB;
            Direction direction;
        };

        // Ce qu'il faut pour mesurer les paires et noter leurs décalages.
        struct PairMeasurement {
            const std::vector<TileInfo>& tiles;
            const PositioningSettings& settings;
            StitchingNs::FeatureMatcherClass& matcher;
            RejectionNs::OutlierRejectorClass& rejector;
            OverlapEstimationNs::OverlapEstimatorClass& overlapEstimator;
            std::vector<TileOffset>& offsets;
            std::ostream& log;
            std::size_t attemptedPairCount = 0;
        };

        // Médiane ; pour un nombre pair de valeurs, moyenne des deux valeurs du milieu.
        double computeMedian(std::vector<double> values) {
            const std::size_t valueCount = values.size();
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(valueCount / 2);
            std::nth_element(values.begin(), middle, values.end());
            double medianValue = *middle;
            if (valueCount % 2 == 0) medianValue = AVERAGE_OF_TWO_WEIGHT * (medianValue + *std::max_element(values.begin(), middle));
            return medianValue;
        }

        // Une passe de propagation depuis les tuiles déjà reliées. true si une nouvelle tuile a été reliée.
        bool propagateConnections(const std::vector<TileOffset>& offsets, std::vector<bool>& connectedTiles) {
            bool hasChanged = false;
            for (const TileOffset& tileOffset : offsets) {
                const bool isConnectedA = connectedTiles[tileOffset.tileIndexA];
                const bool isConnectedB = connectedTiles[tileOffset.tileIndexB];
                if (isConnectedA != isConnectedB) {
                    connectedTiles[tileOffset.tileIndexA] = true;
                    connectedTiles[tileOffset.tileIndexB] = true;
                    hasChanged = true;
                }
            }
            return hasChanged;
        }

        // Tuile 0 = référence.
        std::vector<bool> findConnectedTiles(std::size_t tileCount, const std::vector<TileOffset>& offsets) {
            std::vector<bool> connectedTiles(tileCount, false);
            connectedTiles[0] = true;
            while (propagateConnections(offsets, connectedTiles)) {
            }
            return connectedTiles;
        }

        LinearSystem buildLinearSystem(const std::vector<const TileOffset*>& usableOffsets, const std::vector<int>& variableIndex,
                                       int variableCount) {
            const int equationCount = static_cast<int>(usableOffsets.size());
            LinearSystem system{cv::Mat(equationCount, variableCount, CV_64F, cv::Scalar(0)), cv::Mat(equationCount, 1, CV_64F),
                                cv::Mat(equationCount, 1, CV_64F), cv::Mat(equationCount, 1, CV_64F)};
            for (int equationIndex = 0; equationIndex < equationCount; ++equationIndex) {
                const TileOffset& tileOffset = *usableOffsets[static_cast<std::size_t>(equationIndex)];
                if (tileOffset.tileIndexB != 0) system.coefficients.at<double>(equationIndex, variableIndex[tileOffset.tileIndexB]) += 1.0;
                if (tileOffset.tileIndexA != 0) system.coefficients.at<double>(equationIndex, variableIndex[tileOffset.tileIndexA]) -= 1.0;
                system.rightHandSideX.at<double>(equationIndex, 0) = tileOffset.positionDifference.x;
                system.rightHandSideY.at<double>(equationIndex, 0) = tileOffset.positionDifference.y;
                // Équation theta_B - theta_A = rotation_AB, résolue sur le même graphe
                system.rightHandSideRotation.at<double>(equationIndex, 0) = tileOffset.rotationRadians;
            }
            return system;
        }

        // Résolution simultanée des positions X, Y et des angles de rotation par SVD.
        bool solveLinearSystem(const LinearSystem& system, const std::vector<int>& variableIndex, SolvedTiles& solved) {
            cv::Mat solutionX;
            cv::Mat solutionY;
            cv::Mat solutionRotation;
            if (!cv::solve(system.coefficients, system.rightHandSideX, solutionX, cv::DECOMP_SVD) ||
                !cv::solve(system.coefficients, system.rightHandSideY, solutionY, cv::DECOMP_SVD) ||
                !cv::solve(system.coefficients, system.rightHandSideRotation, solutionRotation, cv::DECOMP_SVD)) {
                return false;
            }
            for (std::size_t tileIndex = 1; tileIndex < variableIndex.size(); ++tileIndex) {
                const int variable = variableIndex[tileIndex];
                if (variable < 0) continue;
                solved.positions[tileIndex] = cv::Point2d(solutionX.at<double>(variable, 0), solutionY.at<double>(variable, 0));
                solved.rotations[tileIndex] = solutionRotation.at<double>(variable, 0);
            }
            return true;
        }

        // Moindres carrés : positionB - positionA = positionDifference pour toutes les paires. Tuile 0 = référence (0, 0).
        // Les angles de rotation absolus de chaque tuile sont résolus en même temps, par SVD.
        bool solvePositions(std::size_t tileCount, const std::vector<TileOffset>& offsets, SolvedTiles& solved) {
            solved.connectedTiles = findConnectedTiles(tileCount, offsets);
            solved.positions.assign(tileCount, cv::Point2d(0.0, 0.0));
            solved.rotations.assign(tileCount, 0.0);   // angles de rotation initialisés à 0.0 rad

            std::vector<int> variableIndex(tileCount, -1);
            int variableCount = 0;
            for (std::size_t tileIndex = 1; tileIndex < tileCount; ++tileIndex) {
                if (solved.connectedTiles[tileIndex]) variableIndex[tileIndex] = variableCount++;
            }
            if (variableCount == 0) return true;

            std::vector<const TileOffset*> usableOffsets;
            for (const TileOffset& tileOffset : offsets) {
                if (solved.connectedTiles[tileOffset.tileIndexA] && solved.connectedTiles[tileOffset.tileIndexB]) {
                    usableOffsets.push_back(&tileOffset);
                }
            }
            return solveLinearSystem(buildLinearSystem(usableOffsets, variableIndex, variableCount), variableIndex, solved);
        }

        // Tuiles : nom -> (ligne, colonne, identifiant)
        bool parseTiles(const std::vector<std::string>& tilePaths, std::vector<TileInfo>& tiles, std::ostream& log) {
            for (const std::string& tilePath : tilePaths) {
                TileInfo tile;
                tile.filePath = tilePath;
                if (!GraphCutNs::ImagePretreatmentClass::parseTileName(tilePath, tile.row, tile.column, tile.tileId)) {
                    log << "wrong tile name: " << tilePath << '\n';
                    return false;
                }
                tiles.push_back(std::move(tile));
            }
            return true;
        }

        // Trie les tuiles par ligne puis colonne et les indexe par position dans la grille. false si une position est en double.
        bool indexTiles(std::vector<TileInfo>& tiles, GridIndex& tileIndexByGridPosition, std::ostream& log) {
            std::sort(tiles.begin(), tiles.end(), [](const TileInfo& first, const TileInfo& second) {
                return first.row != second.row ? first.row < second.row : first.column < second.column;
            });
            for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                if (!tileIndexByGridPosition.emplace(std::make_pair(tiles[tileIndex].row, tiles[tileIndex].column), tileIndex).second) {
                    log << "Duplicate tile grid position " << tiles[tileIndex].tileId << '\n';
                    return false;
                }
            }
            return true;
        }

        // Matchers autorisés : "sift", "correlation_phase", "lightglue" (si STITCHING_PROJECT_ROOT est défini).
        std::unique_ptr<StitchingNs::FeatureMatcherClass> createMatcher(const PositioningSettings& settings) {
            if (settings.matcherName == "sift") return std::make_unique<SiftNs::SiftMatcherClass>(settings.matcherParameters);
            if (settings.matcherName == "correlation_phase") return std::make_unique<PhaseCorrelationNs::PhaseCorrelationMatcherClass>();
#ifdef STITCHING_PROJECT_ROOT
            if (settings.matcherName == "lightglue") return std::make_unique<LightGlueNs::LightGlueMatcherClass>();
#endif
            return nullptr;
        }

        // Rejector : "vote" (Vote 2D / Hough 2D).
        std::unique_ptr<RejectionNs::OutlierRejectorClass> createRejector(const PositioningSettings& settings) {
            if (settings.rejectorName == "vote" || settings.rejectorName == "vote_2d") {
                return std::make_unique<RejectionNs::VoteRejectorClass>(settings.rejectorParameters);
            }
            return nullptr;
        }

        // Apparie la paire. false si elle est ignorée (raison dans le journal).
        bool matchPair(PairMeasurement& measurement, const PairImages& images, const DataNs::OverlapEstimate& overlapEstimate,
                       DataNs::Matches& matches) {
            const std::string& matcherName = measurement.settings.matcherName;
            const DataNs::PairContext context{images.direction, overlapEstimate.overlapFraction, overlapEstimate.translationBInA};
            if (matcherName == "correlation_phase") {
                if (!measurement.matcher.match(images.imageA, images.imageB, context, matches)) {
                    measurement.log << "phase correlation: failed, pair ignored\n";
                    return false;
                }
                return true;
            }
            // Découpe des seules bandes de chevauchement effectif (areaA dans A, areaB dans B) au lieu d'envoyer les
            // tuiles entières, puis translation des points détectés par areaA.tl() et areaB.tl() pour les réexprimer
            // dans le repère global des tuiles entières avant le rejet des aberrants.
            const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                images.imageA.size(), images.imageB.size(), overlapEstimate.translationBInA);
            if (areaA.empty() || !measurement.matcher.match(images.imageA(areaA), images.imageB(areaB), context, matches)) {
                measurement.log << matcherName << ": not enough matches, pair ignored\n";
                return false;
            }
            for (auto& point : matches.pointsInA) point += cv::Point2f(areaA.tl());
            for (auto& point : matches.pointsInB) point += cv::Point2f(areaB.tl());
            return true;
        }

        // Rejette les aberrants puis note le décalage de la paire : médiane de (pointInA - pointInB) sur les inliers.
        void recordOffset(PairMeasurement& measurement, const TilePair& pair, const DataNs::Matches& matches) {
            std::ostream& log = measurement.log;
            DataNs::Alignment alignment;
            if (!measurement.rejector.estimate(matches, alignment) || alignment.inlierMask.size() != matches.pointsInA.size()) {
                log << matches.pointsInA.size() << " matches, Vote2D failed, pair ignored\n";
                return;
            }

            std::vector<double> translationsX;
            std::vector<double> translationsY;
            for (std::size_t pointIndex = 0; pointIndex < matches.pointsInA.size(); ++pointIndex) {
                if (alignment.inlierMask[pointIndex] == 0) continue;
                translationsX.push_back(static_cast<double>(matches.pointsInA[pointIndex].x) - matches.pointsInB[pointIndex].x);
                translationsY.push_back(static_cast<double>(matches.pointsInA[pointIndex].y) - matches.pointsInB[pointIndex].y);
            }
            if (translationsX.empty()) {
                log << "no inlier, pair ignored\n";
                return;
            }

            const cv::Point2d positionDifference(computeMedian(translationsX), computeMedian(translationsY));
            // Angle relatif theta = atan2(m10, m00), extrait de la matrice 3x3 d'alignement
            const double rotationRadians = alignment.transformBToA.empty() ? 0.0
                : std::atan2(alignment.transformBToA.at<double>(1, 0), alignment.transformBToA.at<double>(0, 0));
            measurement.offsets.push_back({pair.tileIndexA, pair.tileIndexB, positionDifference, rotationRadians});
            log << matches.pointsInA.size() << " matches, " << translationsX.size() << " inliers, offset = ("
                << std::fixed << std::setprecision(2) << positionDifference.x << ", " << positionDifference.y << "), rot = "
                << std::setprecision(4) << rotationRadians << " rad\n";
        }

        // Première passe OverlapEstimatorClass -> matcher -> rejector. false seulement si une image est illisible : une
        // paire dont une mesure échoue est ignorée.
        bool processPair(PairMeasurement& measurement, const TilePair& pair) {
            ++measurement.attemptedPairCount;
            const TileInfo& tileA = measurement.tiles[pair.tileIndexA];
            const TileInfo& tileB = measurement.tiles[pair.tileIndexB];
            std::ostream& log = measurement.log;
            log << tileA.tileId << " -> " << tileB.tileId << (pair.direction == Direction::Right ? " (H) : " : " (V) : ");

            // 16 bits sans perte, 2 tuiles à la fois
            using GraphCutNs::ImagePretreatmentClass;
            const PairImages images{ImagePretreatmentClass::read16Bit(tileA.filePath), ImagePretreatmentClass::read16Bit(tileB.filePath), pair.direction};
            if (images.imageA.empty() || images.imageB.empty()) {
                log << "cannot read (" << (images.imageA.empty() ? tileA.filePath : tileB.filePath) << ")\n";
                return false;
            }

            // Première passe : estimation rapide du recouvrement réel au pixel près
            const auto overlapEstimate = measurement.overlapEstimator.estimate(images.imageA, images.imageB, {pair.direction, measurement.settings.overlapFraction});
            if (!overlapEstimate) {
                log << measurement.overlapEstimator.getFailureReason() << ", pair ignored\n";
                return true;
            }

            DataNs::Matches matches;
            if (matchPair(measurement, images, *overlapEstimate, matches)) recordOffset(measurement, pair, matches);
            return true;
        }

        // Mesure chaque tuile avec ses voisines directes (droite, dessous ; pas de diagonale).
        bool processAllPairs(PairMeasurement& measurement, const GridIndex& tileIndexByGridPosition) {
            const std::vector<TileInfo>& tiles = measurement.tiles;
            for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                const auto rightNeighbour = tileIndexByGridPosition.find({tiles[tileIndex].row, tiles[tileIndex].column + 1});
                const auto bottomNeighbour = tileIndexByGridPosition.find({tiles[tileIndex].row + 1, tiles[tileIndex].column});
                if (rightNeighbour != tileIndexByGridPosition.end() &&
                    !processPair(measurement, {tileIndex, rightNeighbour->second, Direction::Right})) {
                    return false;
                }
                if (bottomNeighbour != tileIndexByGridPosition.end() &&
                    !processPair(measurement, {tileIndex, bottomNeighbour->second, Direction::Bottom})) {
                    return false;
                }
            }
            return true;
        }

        // Résidu par paire : écart entre l'offset mesuré et celui de la solution globale (contrôle de cohérence).
        void logResiduals(const std::vector<TileInfo>& tiles, const std::vector<TileOffset>& offsets, const SolvedTiles& solved,
                          std::ostream& log) {
            log << "Adjustment residuals (measured offset - adjusted offset):\n";
            for (const TileOffset& tileOffset : offsets) {
                if (!solved.connectedTiles[tileOffset.tileIndexA] || !solved.connectedTiles[tileOffset.tileIndexB]) continue;
                const cv::Point2d residual = tileOffset.positionDifference -
                                             (solved.positions[tileOffset.tileIndexB] - solved.positions[tileOffset.tileIndexA]);
                log << "  " << tiles[tileOffset.tileIndexA].tileId << " -> " << tiles[tileOffset.tileIndexB].tileId << " : ("
                    << std::fixed << std::setprecision(2) << residual.x << ", " << residual.y << ")\n";
            }
        }

        // Solution globale, angle de rotation compris.
        void collectPositions(const std::vector<TileInfo>& tiles, const SolvedTiles& solved,
                              std::vector<DataNs::TilePositionData>& positions, std::ostream& log) {
            for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                if (solved.connectedTiles[tileIndex]) {
                    positions.push_back({tiles[tileIndex].tileId, solved.positions[tileIndex], solved.rotations[tileIndex]});
                } else {
                    log << "WARNING: tile " << tiles[tileIndex].tileId << " is not linked to the reference tile by any valid pair\n";
                }
            }
        }

        // Mesure toutes les paires et note leurs décalages. false en cas d'erreur (raison dans le journal).
        bool measureOffsets(const TileGrid& grid, const PositioningSettings& settings, std::vector<TileOffset>& offsets,
                            std::ostream& log) {
            const auto matcher = createMatcher(settings);
            const auto rejector = createRejector(settings);
            if (!matcher || !rejector) {
                log << "Matcher or rejector not available (" << settings.matcherName << " / " << settings.rejectorName << ")\n";
                return false;
            }
            OverlapEstimationNs::OverlapEstimatorClass overlapEstimator;
            PairMeasurement measurement{grid.tiles, settings, *matcher, *rejector, overlapEstimator, offsets, log};
            if (!processAllPairs(measurement, grid.index)) return false;

            if (measurement.attemptedPairCount == 0) {
                log << "No horizontally or vertically adjacent tile pairs were found\n";
                return false;
            }
            if (offsets.empty()) {
                log << "SIFT + Vote2D did not give a usable offset for any neighboring pair\n";
                return false;
            }
            return true;
        }

    }

    bool computeTilePositions(const std::vector<std::string>& tilePaths, const PositioningSettings& settings,
                              std::vector<DataNs::TilePositionData>& positions, std::ostream& log) {
        positions.clear();

        TileGrid grid;
        if (!parseTiles(tilePaths, grid.tiles, log)) return false;
        if (grid.tiles.empty()) {
            log << "No tiles\n";
            return false;
        }

        if (!indexTiles(grid.tiles, grid.index, log)) return false;
        if (grid.tiles.size() == 1) {
            positions.push_back({grid.tiles[0].tileId, cv::Point2d(0.0, 0.0), 0.0});
            return true;
        }

        std::vector<TileOffset> offsets;
        if (!measureOffsets(grid, settings, offsets, log)) return false;

        // Ajustement global
        SolvedTiles solved;
        if (!solvePositions(grid.tiles.size(), offsets, solved)) {
            log << "Could not solve tile positions\n";
            return false;
        }

        logResiduals(grid.tiles, offsets, solved, log);
        collectPositions(grid.tiles, solved, positions, log);
        return !positions.empty();
    }

}
