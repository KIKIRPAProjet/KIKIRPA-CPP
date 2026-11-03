// src/backend/algorithms/stitching/positioning/TilePositioner.cpp
#include "algorithms/stitching/positioning/TilePositioner.h"

#include "algorithms/legacy_type_aliases.h"
#include "algorithms/rejection/OutlierRejector.h"
#include "algorithms/rejection/ransac/RansacRejector.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include "algorithms/stitching/FeatureMatcher.h"
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "algorithms/stitching/positioning/position_solver.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "algorithms/stitching/sift/SiftMatcher.h"
#ifdef STITCHING_PROJECT_ROOT
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#endif

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stitching::positioning {

    namespace {

        constexpr double MEDIAN_WEIGHT = 0.5;

        struct TileInfo {
            std::string path;
            std::string id;
            int row = 0;
            int column = 0;
        };

        struct PairIndices {
            std::size_t first = 0;
            std::size_t second = 0;
            Direction direction = Direction::Right;
        };

        struct MatchingEngine {
            PositioningSettings settings;
            std::unique_ptr<matching::FeatureMatcher> matcher;
            std::unique_ptr<rejection::OutlierRejector> rejector;
        };

        using GridIndex = std::map<std::pair<int, int>, std::size_t>;
        using Offsets = std::vector<PositioningNs::RelativeTilePosition>;

        double computeMedian(std::vector<double> values) {
            auto count = values.size();
            auto middle = values.begin() + static_cast<std::ptrdiff_t>(count / 2);
            std::nth_element(values.begin(), middle, values.end());
            auto median = *middle;
            if (count % 2 == 0) median = MEDIAN_WEIGHT * (median + *std::max_element(values.begin(), middle));
            return median;
        }

        std::vector<TileInfo> readTiles(const std::vector<std::string>& tilePaths) {
            std::vector<TileInfo> tiles;
            tiles.reserve(tilePaths.size());
            for (auto& path : tilePaths) {
                TileInfo tile;
                tile.path = path;
                if (!Seamline::ImagePretreatment::parseTileName(path, tile.row, tile.column, tile.id))
                    throw std::invalid_argument("wrong tile name: " + path);
                tiles.push_back(std::move(tile));
            }
            if (tiles.empty()) throw std::invalid_argument("No tiles");
            std::sort(tiles.begin(), tiles.end(), [](const TileInfo& first, const TileInfo& second) {
                return first.row != second.row ? first.row < second.row : first.column < second.column;
            });
            return tiles;
        }

        GridIndex buildGrid(const std::vector<TileInfo>& tiles) {
            GridIndex grid;
            for (std::size_t index = 0; index < tiles.size(); ++index)
                if (!grid.emplace(std::make_pair(tiles[index].row, tiles[index].column), index).second)
                    throw std::invalid_argument("Duplicate tile grid position " + tiles[index].id);
            return grid;
        }

        MatchingEngine createEngine(const PositioningSettings& settings) {
            MatchingEngine engine;
            engine.settings = settings;
            if (settings.matcher == "sift") engine.matcher = std::make_unique<matching::SiftMatcher>(settings.matcherParams);
            else if (settings.matcher == "correlation_phase") engine.matcher = std::make_unique<matching::CorrelationPhase>();
#ifdef STITCHING_PROJECT_ROOT
            else if (settings.matcher == "lightglue") engine.matcher = std::make_unique<matching::LightGlueMatcher>();
#endif
            if (settings.rejector == "ransac") engine.rejector = std::make_unique<rejection::RansacRejector>(settings.rejectorParams);
            else if (settings.rejector == "vote") engine.rejector = std::make_unique<rejection::VoteRejector>(settings.rejectorParams);
            if (!engine.matcher || !engine.rejector) throw std::invalid_argument("Matcher or rejector unavailable");
            return engine;
        }

        bool findMatches(MatchingEngine& engine, const std::array<cv::Mat, 2>& images, Direction direction, Matches& matches) {
            matching::EstimationRecouvrement estimation;
            auto overlap = estimation.estimer(images[0], images[1], {direction, engine.settings.overlap});
            if (!overlap) return false;
            PairContext context{direction, overlap->overlapFraction, overlap->translationBInA};
            if (engine.settings.matcher == "correlation_phase")
                return engine.matcher->match(images[0], images[1], context, matches);
            auto [zoneA, zoneB] = matching::EstimationRecouvrement::zonesCommunes(images[0].size(), images[1].size(), overlap->translationBInA);
            if (zoneA.empty() || zoneB.empty() || !engine.matcher->match(images[0](zoneA), images[1](zoneB), context, matches)) return false;
            // Replacer les points dans les repères des tuiles avant le rejet.
            for (auto& point : matches.pointsInA) point += cv::Point2f(zoneA.tl());
            for (auto& point : matches.pointsInB) point += cv::Point2f(zoneB.tl());
            return true;
        }

        std::optional<PositioningNs::RelativeTilePosition> rejectMatches(MatchingEngine& engine, const Matches& matches,
                                                                        const PairIndices& pair) {
            Alignment alignment;
            if (matches.pointsInA.size() != matches.pointsInB.size() || !engine.rejector->estimate(matches, alignment) ||
                alignment.inlierMask.size() != matches.pointsInA.size()) return std::nullopt;
            std::vector<double> translationsX, translationsY;
            for (std::size_t index = 0; index < matches.pointsInA.size(); ++index) {
                if (!alignment.inlierMask[index]) continue;
                translationsX.push_back(static_cast<double>(matches.pointsInA[index].x) - matches.pointsInB[index].x);
                translationsY.push_back(static_cast<double>(matches.pointsInA[index].y) - matches.pointsInB[index].y);
            }
            if (translationsX.empty()) return std::nullopt;
            auto rotation = alignment.transformBToA.empty() ? 0.0
                : std::atan2(alignment.transformBToA.at<double>(1, 0), alignment.transformBToA.at<double>(0, 0));
            return PositioningNs::RelativeTilePosition{pair.first, pair.second,
                {computeMedian(translationsX), computeMedian(translationsY)}, rotation};
        }

        void measurePair(MatchingEngine& engine, const std::vector<TileInfo>& tiles, const PairIndices& pair,
                         std::pair<Offsets&, std::ostream&> output) {
            output.second << tiles[pair.first].id << " -> " << tiles[pair.second].id << ": ";
            std::array<cv::Mat, 2> images = {Seamline::ImagePretreatment::read16(tiles[pair.first].path),
                                           Seamline::ImagePretreatment::read16(tiles[pair.second].path)};
            if (images[0].empty() || images[1].empty()) throw std::runtime_error("Could not read tile images");
            Matches matches;
            if (!findMatches(engine, images, pair.direction, matches)) {
                output.second << "matching failed, pair ignored\n";
                return;
            }
            auto offset = rejectMatches(engine, matches, pair);
            if (!offset) { output.second << "rejection failed, pair ignored\n"; return; }
            output.first.push_back(*offset);
            output.second << matches.pointsInA.size() << " matches, offset = (" << offset->translation.x << ", "
                          << offset->translation.y << "), rotation = " << offset->rotationRadians << " rad\n";
        }

        Offsets collectOffsets(const std::vector<TileInfo>& tiles, const GridIndex& grid, MatchingEngine& engine, std::ostream& log) {
            Offsets offsets;
            for (std::size_t index = 0; index < tiles.size(); ++index) {
                auto right = grid.find({tiles[index].row, tiles[index].column + 1});
                auto below = grid.find({tiles[index].row + 1, tiles[index].column});
                if (right != grid.end()) measurePair(engine, tiles, {index, right->second, Direction::Right}, {offsets, log});
                if (below != grid.end()) measurePair(engine, tiles, {index, below->second, Direction::Bottom}, {offsets, log});
            }
            if (offsets.empty()) throw std::runtime_error("No usable offset for any neighboring pair");
            return offsets;
        }

        void logResiduals(const std::vector<TileInfo>& tiles, const Offsets& offsets,
                          const PositioningNs::PositionSolution& solution, std::ostream& log) {
            log << "Residuals (measured offset - adjusted offset):\n";
            for (auto& offset : offsets) {
                if (!solution.connected[offset.tileIndexA] || !solution.connected[offset.tileIndexB]) continue;
                auto residual = offset.translation - (solution.positions[offset.tileIndexB] - solution.positions[offset.tileIndexA]);
                log << tiles[offset.tileIndexA].id << " -> " << tiles[offset.tileIndexB].id << ": (" << std::fixed
                    << std::setprecision(2) << residual.x << ", " << residual.y << ")\n";
            }
        }

        void publishPositions(const std::vector<TileInfo>& tiles, const PositioningNs::PositionSolution& solution,
                              std::vector<TilePositionData>& positions, std::ostream& log) {
            for (std::size_t index = 0; index < tiles.size(); ++index) {
                if (solution.connected[index]) positions.push_back({tiles[index].id, solution.positions[index], solution.rotations[index]});
                else log << "Disconnected tile: " << tiles[index].id << '\n';
            }
        }

    }

    bool computeTilePositions(const std::vector<std::string>& tilePaths, const PositioningSettings& settings,
                              std::vector<TilePositionData>& positions, std::ostream& log) {
        positions.clear();
        try {
            auto tiles = readTiles(tilePaths);
            auto grid = buildGrid(tiles);
            if (tiles.size() == 1) {
                positions.push_back({tiles[0].id, cv::Point2d(0.0, 0.0), 0.0});
                return true;
            }
            auto engine = createEngine(settings);
            auto offsets = collectOffsets(tiles, grid, engine, log);
            auto solution = PositioningNs::solveRelativePositions(tiles.size(), offsets);
            logResiduals(tiles, offsets, solution, log);
            publishPositions(tiles, solution, positions, log);
            return !positions.empty();
        } catch (const std::exception& exception) {
            log << exception.what() << '\n';
            positions.clear();
            return false;
        }
    }

}
