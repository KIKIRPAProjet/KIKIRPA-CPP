// src/backend/algorithms/seamline/dynamic_programming_dijkstra/territory_builder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h"

#include "core/data/mosaic_corners.h"

#include <opencv2/core.hpp>
#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        constexpr int NO_TILE = -1;
        constexpr int VALID_PIXEL = 255;

        struct TerritoryGeometry {
            std::vector<cv::Rect> tileRects;
            cv::Size canvasSize;
        };

        struct PairCut {
            int tileIndexA = 0;
            int tileIndexB = 0;
            cv::Rect overlap;
            std::vector<cv::Point> polygonA;
        };

        using TilePair = std::pair<int, int>;
        using PairCuts = std::map<TilePair, PairCut>;

        TilePair buildPairKey(int tileIndexA, int tileIndexB) {
            return { std::min(tileIndexA, tileIndexB),
                    std::max(tileIndexA, tileIndexB) };
        }

        bool validateInput(const TerritoryInput& input, std::string& error) {
            if (input.tiles.empty() ||
                input.positions.size() != input.tiles.size() ||
                input.tiles.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                error = "territories: invalid tile or position count";
                return false;
            }

            std::map<int, std::size_t> tileIndexById;
            for (std::size_t tileIndex = 0;
                tileIndex < input.tiles.size(); ++tileIndex) {
                const auto& tile = input.tiles[tileIndex];
                const cv::Point2d position = input.positions[tileIndex].position;
                if (!tile || !tileIndexById.emplace(tile->getId(), tileIndex).second ||
                    !std::isfinite(position.x) || !std::isfinite(position.y)) {
                    error = "territories: null tile, duplicate id or invalid position";
                    return false;
                }

                const DataNs::Level& level = tile->getLevel(0);
                if (level.width <= 0 || level.height <= 0) {
                    error = "territories: tile dimensions must be positive";
                    return false;
                }
            }
            return true;
        }

        bool buildGeometry(const TerritoryInput& input,
            TerritoryGeometry& geometry,
            std::string& error) {
            std::vector<cv::Point2d> positions;
            for (const auto& position : input.positions)
                positions.push_back(position.position);

            const auto minimumX = std::min_element(
                positions.begin(), positions.end(),
                [](const cv::Point2d& first, const cv::Point2d& second) {
                    return first.x < second.x;
                });
            const auto minimumY = std::min_element(
                positions.begin(), positions.end(),
                [](const cv::Point2d& first, const cv::Point2d& second) {
                    return first.y < second.y;
                });

            for (std::size_t tileIndex = 0;
                tileIndex < input.tiles.size(); ++tileIndex) {
                const DataNs::Level& level = input.tiles[tileIndex]->getLevel(0);
                const double right = positions[tileIndex].x - minimumX->x +
                    level.width;
                const double bottom = positions[tileIndex].y - minimumY->y +
                    level.height;
                if (!std::isfinite(right) || !std::isfinite(bottom) ||
                    right > std::numeric_limits<int>::max() - 1.0 ||
                    bottom > std::numeric_limits<int>::max() - 1.0) {
                    error = "territories: mosaic dimensions exceed integer coordinates";
                    return false;
                }
            }

            const std::vector<cv::Point> corners =
                DataNs::computeMosaicCorners(positions);
            for (std::size_t tileIndex = 0;
                tileIndex < input.tiles.size(); ++tileIndex) {
                const DataNs::Level& level = input.tiles[tileIndex]->getLevel(0);
                const cv::Rect rectangle(corners[tileIndex],
                    cv::Size(level.width, level.height));
                geometry.tileRects.push_back(rectangle);
                geometry.canvasSize.width =
                    std::max(geometry.canvasSize.width, rectangle.br().x);
                geometry.canvasSize.height =
                    std::max(geometry.canvasSize.height, rectangle.br().y);
            }
            return true;
        }

        // Le chemin reste complet : Dijkstra peut visiter plusieurs pixels
        // d'une même ligne. Une seule abscisse par ligne perdrait cette géométrie.
        void closePolygon(const DataNs::Seamline& seamline, PairCut& cut) {
            const cv::Point first = cut.polygonA.front();
            const cv::Point last = cut.polygonA.back();
            const int left = cut.overlap.x;
            const int top = cut.overlap.y;
            const int right = cut.overlap.br().x - 1;
            const int bottom = cut.overlap.br().y - 1;

            if (seamline.direction == DataNs::Direction::Right) {
                cut.polygonA.insert(cut.polygonA.begin(), { first.x, top });
                cut.polygonA.push_back({ last.x, bottom });
                cut.polygonA.push_back({ left, bottom });
                cut.polygonA.push_back({ left, top });
            }
            else {
                cut.polygonA.insert(cut.polygonA.begin(), { left, first.y });
                cut.polygonA.push_back({ right, last.y });
                cut.polygonA.push_back({ right, top });
                cut.polygonA.push_back({ left, top });
            }
        }

        bool buildPairCut(const TerritoryInput& input,
            const TerritoryGeometry& geometry,
            const DataNs::Seamline& seamline,
            PairCut& cut,
            std::string& error) {
            for (std::size_t tileIndex = 0;
                tileIndex < input.tiles.size(); ++tileIndex) {
                const int tileId = input.tiles[tileIndex]->getId();
                if (tileId == seamline.tileIdA)
                    cut.tileIndexA = static_cast<int>(tileIndex);
                if (tileId == seamline.tileIdB)
                    cut.tileIndexB = static_cast<int>(tileIndex);
            }

            const cv::Rect& rectangleA = geometry.tileRects[cut.tileIndexA];
            cut.overlap = rectangleA & geometry.tileRects[cut.tileIndexB];
            if (cut.overlap.empty()) {
                error = "territories: seamline tiles do not overlap";
                return false;
            }

            const cv::Point2d positionA =
                input.positions[cut.tileIndexA].position;
            for (const cv::Point2d& point : seamline.points) {
                // Même arrondi du coin de tuile que FusionStep.
                const cv::Point2d localPoint = point - positionA;
                if (!std::isfinite(localPoint.x) ||
                    !std::isfinite(localPoint.y) ||
                    localPoint.x < 0.0 || localPoint.y < 0.0 ||
                    localPoint.x >= rectangleA.width ||
                    localPoint.y >= rectangleA.height) {
                    error = "territories: seamline point is outside tile A";
                    return false;
                }
                cut.polygonA.emplace_back(
                    rectangleA.x + cvRound(localPoint.x),
                    rectangleA.y + cvRound(localPoint.y));
            }
            closePolygon(seamline, cut);
            return true;
        }

        bool buildPairCuts(const TerritoryInput& input,
            const TerritoryGeometry& geometry,
            PairCuts& cuts,
            std::string& error) {
            std::map<int, int> tileIndexById;
            for (std::size_t tileIndex = 0;
                tileIndex < input.tiles.size(); ++tileIndex)
                tileIndexById.emplace(input.tiles[tileIndex]->getId(),
                    static_cast<int>(tileIndex));

            for (const DataNs::Seamline& seamline : input.seamlines) {
                const auto entryA = tileIndexById.find(seamline.tileIdA);
                const auto entryB = tileIndexById.find(seamline.tileIdB);
                if (entryA == tileIndexById.end() ||
                    entryB == tileIndexById.end() ||
                    entryA->second == entryB->second ||
                    seamline.points.empty() ||
                    (seamline.direction != DataNs::Direction::Right &&
                        seamline.direction != DataNs::Direction::Bottom)) {
                    error = "territories: invalid seamline";
                    return false;
                }

                PairCut cut;
                if (!buildPairCut(input, geometry, seamline, cut, error))
                    return false;
                const TilePair key =
                    buildPairKey(cut.tileIndexA, cut.tileIndexB);
                if (!cuts.emplace(key, std::move(cut)).second) {
                    error = "territories: duplicate seamline for tile pair";
                    return false;
                }
            }
            return true;
        }

        cv::Mat rasterizeSideA(const PairCut& cut, const cv::Rect& area) {
            cv::Mat mask = cv::Mat::zeros(area.size(), CV_8UC1);
            const std::vector<std::vector<cv::Point>> polygons{ cut.polygonA };
            cv::fillPoly(mask, polygons, cv::Scalar(VALID_PIXEL),
                cv::LINE_8, 0, -area.tl());
            return mask;
        }

        void applyPairCut(cv::Mat& owners, const cv::Rect& block,
            const PairCut& cut, int candidateIndex) {
            const cv::Rect area = block & cut.overlap;
            if (area.empty()) return;

            const cv::Mat sideA = rasterizeSideA(cut, area);
            cv::Mat areaOwners = owners(cv::Rect(area.tl() - block.tl(),
                area.size()));
            const int previousIndex = candidateIndex == cut.tileIndexA ?
                cut.tileIndexB : cut.tileIndexA;

            for (int rowIndex = 0; rowIndex < area.height; ++rowIndex) {
                int* ownerRow = areaOwners.ptr<int>(rowIndex);
                const unsigned char* sideRow =
                    sideA.ptr<unsigned char>(rowIndex);
                for (int columnIndex = 0;
                    columnIndex < area.width; ++columnIndex) {
                    const bool candidateWins =
                        (sideRow[columnIndex] != 0) ==
                        (candidateIndex == cut.tileIndexA);
                    if (ownerRow[columnIndex] == previousIndex &&
                        candidateWins)
                        ownerRow[columnIndex] = candidateIndex;
                }
            }
        }

        cv::Mat buildBlockOwners(const TerritoryGeometry& geometry,
            const PairCuts& cuts,
            const cv::Rect& block) {
            cv::Mat owners(block.size(), CV_32SC1, cv::Scalar(NO_TILE));
            for (std::size_t tileIndex = 0;
                tileIndex < geometry.tileRects.size(); ++tileIndex) {
                const cv::Rect area = block & geometry.tileRects[tileIndex];
                if (area.empty()) continue;

                const int candidateIndex = static_cast<int>(tileIndex);
                cv::Mat areaOwners = owners(cv::Rect(area.tl() - block.tl(),
                    area.size()));
                areaOwners.setTo(candidateIndex, areaOwners == NO_TILE);

                for (std::size_t previousIndex = 0;
                    previousIndex < tileIndex; ++previousIndex) {
                    const auto cut = cuts.find(buildPairKey(
                        static_cast<int>(previousIndex), candidateIndex));
                    if (cut != cuts.end())
                        applyPairCut(owners, block, cut->second, candidateIndex);
                }
            }
            return owners;
        }

        void appendPolygons(const cv::Mat& mask, const cv::Point& origin,
            DataNs::TileTerritory& territory) {
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::findContours(mask, contours, hierarchy, cv::RETR_CCOMP,
                cv::CHAIN_APPROX_SIMPLE, origin);

            for (std::size_t contourIndex = 0;
                contourIndex < contours.size(); ++contourIndex) {
                if (hierarchy[contourIndex][3] >= 0) continue;

                std::vector<std::vector<cv::Point>> holes;
                for (int holeIndex = hierarchy[contourIndex][2];
                    holeIndex >= 0; holeIndex = hierarchy[holeIndex][0])
                    holes.push_back(contours[holeIndex]);

                territory.polygons.push_back(contours[contourIndex]);
                territory.holes.push_back(std::move(holes));
            }
        }

        void appendBlockTerritories(
            const TerritoryGeometry& geometry,
            const cv::Mat& owners,
            const cv::Rect& block,
            std::vector<DataNs::TileTerritory>& territories) {
            for (std::size_t tileIndex = 0;
                tileIndex < geometry.tileRects.size(); ++tileIndex) {
                if ((block & geometry.tileRects[tileIndex]).empty()) continue;
                const cv::Mat mask = owners == static_cast<int>(tileIndex);
                appendPolygons(mask, block.tl(), territories[tileIndex]);
            }
        }

    }

    bool buildTerritories(const TerritoryInput& input,
        std::vector<DataNs::TileTerritory>& territories,
        std::string& error) {
        territories.clear();
        error.clear();
        if (!validateInput(input, error)) return false;

        TerritoryGeometry geometry;
        PairCuts cuts;
        if (!buildGeometry(input, geometry, error) ||
            !buildPairCuts(input, geometry, cuts, error))
            return false;

        territories.resize(input.tiles.size());
        for (std::size_t tileIndex = 0;
            tileIndex < territories.size(); ++tileIndex)
            territories[tileIndex].tileIndex = static_cast<int>(tileIndex);

        int blockTop = 0;
        while (blockTop < geometry.canvasSize.height) {
            const int blockHeight = std::min(
                DataNs::BLOCK_SIZE, geometry.canvasSize.height - blockTop);
            int blockLeft = 0;
            while (blockLeft < geometry.canvasSize.width) {
                const int blockWidth = std::min(
                    DataNs::BLOCK_SIZE, geometry.canvasSize.width - blockLeft);
                const cv::Rect block(blockLeft, blockTop,
                    blockWidth, blockHeight);
                const cv::Mat owners = buildBlockOwners(geometry, cuts, block);
                appendBlockTerritories(geometry, owners, block, territories);
                blockLeft += blockWidth;
            }
            blockTop += blockHeight;
        }
        return true;
    }

}