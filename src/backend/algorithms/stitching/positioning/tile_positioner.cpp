// src/backend/algorithms/stitching/positioning/tile_positioner.cpp
#include "algorithms/stitching/positioning/tile_positioner.h"

#include "algorithms/rejection/outlier_rejector.h"
#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "algorithms/stitching/feature_matcher.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "algorithms/stitching/positioning/position_solver.h"
#include "algorithms/stitching/sift/sift_matcher.h"
#include "core/data/algorithm_types.h"
#ifdef STITCHING_PROJECT_ROOT
#include "algorithms/stitching/lightglue/lightglue_matcher.h"
#endif

#include <opencv2/core/base.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
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

        using TileOffsets = std::vector<RelativeTilePosition>;
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
            TileOffsets& offsets;
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
        void logResiduals(const std::vector<TileInfo>& tiles, const TileOffsets& offsets, const PositionSolution& solved,
                          std::ostream& log) {
            log << "Adjustment residuals (measured offset - adjusted offset):\n";
            for (const RelativeTilePosition& tileOffset : offsets) {
                if (!solved.connected[tileOffset.tileIndexA] || !solved.connected[tileOffset.tileIndexB]) continue;
                const cv::Point2d residual = tileOffset.translation -
                                             (solved.positions[tileOffset.tileIndexB] - solved.positions[tileOffset.tileIndexA]);
                log << "  " << tiles[tileOffset.tileIndexA].tileId << " -> " << tiles[tileOffset.tileIndexB].tileId << " : ("
                    << std::fixed << std::setprecision(2) << residual.x << ", " << residual.y << ")\n";
            }
        }

        // Solution globale, angle de rotation compris.
        void collectPositions(const std::vector<TileInfo>& tiles, const PositionSolution& solved,
                              std::vector<DataNs::TilePositionData>& positions, std::ostream& log) {
            for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                if (solved.connected[tileIndex]) {
                    positions.push_back({tiles[tileIndex].tileId, solved.positions[tileIndex], solved.rotations[tileIndex]});
                } else {
                    log << "WARNING: tile " << tiles[tileIndex].tileId << " is not linked to the reference tile by any valid pair\n";
                }
            }
        }

        // Mesure toutes les paires et note leurs décalages. false en cas d'erreur (raison dans le journal).
        bool measureOffsets(const TileGrid& grid, const PositioningSettings& settings, TileOffsets& offsets,
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

    namespace {

        // Ajustement global, puis résidus par paire et positions finales.
        bool solveAndPublish(const std::vector<TileInfo>& tiles, const TileOffsets& offsets,
                             std::vector<DataNs::TilePositionData>& positions, std::ostream& log) {
            PositionSolution solved;
            try {
                solved = solveRelativePositions(tiles.size(), offsets);
            } catch (const std::exception&) {
                log << "Could not solve tile positions\n";
                return false;
            }

            logResiduals(tiles, offsets, solved, log);
            collectPositions(tiles, solved, positions, log);
            return !positions.empty();
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

        TileOffsets offsets;
        if (!measureOffsets(grid, settings, offsets, log)) return false;

        return solveAndPublish(grid.tiles, offsets, positions, log);
    }

}
