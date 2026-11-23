// src/backend/algorithms/seamline/dynamic_programming_dijkstra/territory_builder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h"

#include "core/data/algorithm_types.h"
#include "core/data/mosaic_corners.h"
#include "core/data/tile.h"

#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
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

        std::vector<cv::Point2d> collectPositions(const TerritoryInput& input) {
            std::vector<cv::Point2d> positions;
            positions.reserve(input.positions.size());
            for (const auto& position : input.positions)
                positions.push_back(position.position);
            return positions;
        }

        cv::Point2d findMinimumPosition(const std::vector<cv::Point2d>& positions) {
            cv::Point2d minimum = positions.front();
            for (const cv::Point2d& position : positions) {
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
            }
            return minimum;
        }

        // Contrôle les bornes avant cvRound et les additions entières des rectangles.
        bool validateDimensions(const TerritoryInput& input, std::string& error) {
            const cv::Point2d minimum = findMinimumPosition(collectPositions(input));
            for (std::size_t tileIndex = 0; tileIndex < input.tiles.size(); ++tileIndex) {
                const DataNs::Level& level = input.tiles[tileIndex]->getLevel(0);
                const cv::Point2d offset = input.positions[tileIndex].position - minimum;
                const double right = offset.x + level.width;
                const double bottom = offset.y + level.height;
                if (!std::isfinite(right) || !std::isfinite(bottom) ||
                    right > std::numeric_limits<int>::max() - 1.0 ||
                    bottom > std::numeric_limits<int>::max() - 1.0) {
                    error = "territories: mosaic dimensions exceed integer coordinates";
                    return false;
                }
            }
            return true;
        }

        // La fusion soustrait le minimum avant d'arrondir : arrondir les positions
        // absolues séparément pourrait décaler les territoires d'un pixel.
        bool buildGeometry(const TerritoryInput& input, TerritoryGeometry& geometry,
            std::string& error) {
            if (!validateDimensions(input, error)) return false;
            const std::vector<cv::Point> corners =
                DataNs::computeMosaicCorners(collectPositions(input));
            geometry.tileRects.reserve(input.tiles.size());
            for (std::size_t tileIndex = 0; tileIndex < input.tiles.size(); ++tileIndex) {
                const DataNs::Level& level = input.tiles[tileIndex]->getLevel(0);
                const cv::Rect rectangle(corners[tileIndex], cv::Size(level.width, level.height));
                geometry.tileRects.push_back(rectangle);
                geometry.canvasSize.width = std::max(geometry.canvasSize.width, rectangle.br().x);
                geometry.canvasSize.height = std::max(geometry.canvasSize.height, rectangle.br().y);
            }
            return true;
        }

        // Le chemin reste complet : Dijkstra peut visiter plusieurs pixels
        // d'une même ligne. Une seule abscisse par ligne perdrait cette géométrie.
        // Le chemin est orienté du haut vers le bas (Right), ou de gauche à droite
        // après détransposition (Bottom). Le fermer vers le bord gauche ou haut
        // décrit le côté A ; le complément dans le recouvrement décrit le côté B.
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

        struct PairCutInput {
            const DataNs::Seamline& seamline;
            const cv::Rect& rectangleA;
            cv::Point2d positionA;
        };

        // Préserve chaque point du chemin, puis le replace relativement au coin
        // entier de A : la seamline d'origine conserve son repère absolu.
        bool appendPath(const PairCutInput& input, PairCut& cut, std::string& error) {
            cut.polygonA.reserve(input.seamline.points.size() + 4);
            for (const cv::Point2d& point : input.seamline.points) {
                const cv::Point2d localPoint = point - input.positionA;
                if (!std::isfinite(localPoint.x) || !std::isfinite(localPoint.y) ||
                    localPoint.x < 0.0 || localPoint.y < 0.0 ||
                    localPoint.x >= input.rectangleA.width || localPoint.y >= input.rectangleA.height) {
                    error = "territories: seamline point is outside tile A";
                    return false;
                }
                cut.polygonA.emplace_back(input.rectangleA.x + cvRound(localPoint.x),
                    input.rectangleA.y + cvRound(localPoint.y));
            }
            return true;
        }

        bool buildPairCut(const PairCutInput& input, PairCut& cut, std::string& error) {
            if (cut.overlap.empty()) {
                error = "territories: seamline tiles do not overlap";
                return false;
            }
            if (!appendPath(input, cut, error)) return false;
            closePolygon(input.seamline, cut);
            return true;
        }

        using TileIndexById = std::map<int, int>;

        TileIndexById indexTileIds(const DataNs::TileList& tiles) {
            TileIndexById tileIndexById;
            for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
                tileIndexById.emplace(tiles[tileIndex]->getId(), static_cast<int>(tileIndex));
            return tileIndexById;
        }

        // Les identifiants des coutures ne sont pas les indices attendus par fusion.
        bool findPairIndices(const DataNs::Seamline& seamline,
            const TileIndexById& tileIndexById, PairCut& cut) {
            const auto entryA = tileIndexById.find(seamline.tileIdA);
            const auto entryB = tileIndexById.find(seamline.tileIdB);
            if (entryA == tileIndexById.end() || entryB == tileIndexById.end() ||
                entryA->second == entryB->second || seamline.points.empty() ||
                (seamline.direction != DataNs::Direction::Right &&
                    seamline.direction != DataNs::Direction::Bottom))
                return false;
            cut.tileIndexA = entryA->second;
            cut.tileIndexB = entryB->second;
            return true;
        }

        bool buildPairCuts(const TerritoryInput& input, const TerritoryGeometry& geometry,
            PairCuts& cuts, std::string& error) {
            const TileIndexById tileIndexById = indexTileIds(input.tiles);
            for (const DataNs::Seamline& seamline : input.seamlines) {
                PairCut cut;
                if (!findPairIndices(seamline, tileIndexById, cut)) {
                    error = "territories: invalid seamline";
                    return false;
                }
                const cv::Rect& rectangleA = geometry.tileRects[cut.tileIndexA];
                cut.overlap = rectangleA & geometry.tileRects[cut.tileIndexB];
                const PairCutInput pairInput{ seamline, rectangleA,
                                             input.positions[cut.tileIndexA].position };
                if (!buildPairCut(pairInput, cut, error)) return false;
                const TilePair key = buildPairKey(cut.tileIndexA, cut.tileIndexB);
                if (!cuts.emplace(key, std::move(cut)).second) {
                    error = "territories: duplicate seamline for tile pair";
                    return false;
                }
            }
            return true;
        }

        // Le décalage rasterise uniquement le bloc demandé, sans masque pleine
        // mosaïque. fillPoly inclut les pixels du chemin dans le côté A.
        cv::Mat rasterizeSideA(const PairCut& cut, const cv::Rect& area) {
            cv::Mat mask = cv::Mat::zeros(area.size(), CV_8UC1);
            const std::vector<std::vector<cv::Point>> polygons{ cut.polygonA };
            cv::fillPoly(mask, polygons, cv::Scalar(VALID_PIXEL),
                cv::LINE_8, 0, -area.tl());
            return mask;
        }

        // Une paire ne peut retirer un pixel qu'à son autre tuile. Un propriétaire
        // tiers reste inchangé : cela évite de réattribuer des pixels hors de la paire.
        void applyPairCut(cv::Mat& owners, const cv::Rect& block,
            const PairCut& cut, int candidateIndex) {
            const cv::Rect area = block & cut.overlap;
            if (area.empty()) return;

            const cv::Mat sideA = rasterizeSideA(cut, area);
            cv::Mat areaOwners = owners(cv::Rect(area.tl() - block.tl(), area.size()));
            const int previousIndex = candidateIndex == cut.tileIndexA ?
                cut.tileIndexB : cut.tileIndexA;

            for (int rowIndex = 0; rowIndex < area.height; ++rowIndex) {
                int* ownerRow = areaOwners.ptr<int>(rowIndex);
                const unsigned char* sideRow = sideA.ptr<unsigned char>(rowIndex);
                for (int columnIndex = 0; columnIndex < area.width; ++columnIndex) {
                    const bool candidateWins =
                        (sideRow[columnIndex] != 0) ==
                        (candidateIndex == cut.tileIndexA);
                    if (ownerRow[columnIndex] == previousIndex && candidateWins)
                        ownerRow[columnIndex] = candidateIndex;
                }
            }
        }

        // Une case contient un seul propriétaire. Les tuiles remplissent d'abord
        // les cases libres, puis leurs coutures peuvent remplacer un propriétaire
        // précédent. Sans couture entre les deux, la première tuile reste prioritaire.
        cv::Mat buildBlockOwners(const TerritoryGeometry& geometry,
            const PairCuts& cuts, const cv::Rect& block) {
            cv::Mat owners(block.size(), CV_32SC1, cv::Scalar(NO_TILE));
            for (std::size_t tileIndex = 0;
                tileIndex < geometry.tileRects.size(); ++tileIndex) {
                const cv::Rect area = block & geometry.tileRects[tileIndex];
                if (area.empty()) continue;

                const int candidateIndex = static_cast<int>(tileIndex);
                cv::Mat areaOwners = owners(cv::Rect(area.tl() - block.tl(), area.size()));
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

        // RETR_CCOMP rattache les trous à leur contour extérieur. Aucun filtre
        // d'aire : supprimer un petit contour ferait perdre des pixels à la fusion.
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

        using PositionByTileId = std::map<int, const DataNs::TilePositionData*>;

        bool indexPositions(const std::vector<DataNs::TilePositionData>& positions,
            PositionByTileId& positionByTileId, std::string& error) {
            for (const auto& position : positions) {
                if (!positionByTileId.emplace(position.tileId, &position).second) {
                    error = "positions: duplicate tile id " + std::to_string(position.tileId);
                    return false;
                }
            }
            return true;
        }

        // Les blocs ne partagent aucun pixel. Leurs polygones restent séparés,
        // même pour une même tuile ; leur union reconstitue son territoire.
        void buildAllBlocks(const TerritoryGeometry& geometry, const PairCuts& cuts,
            std::vector<DataNs::TileTerritory>& territories) {
            int blockTop = 0;
            while (blockTop < geometry.canvasSize.height) {
                const int blockHeight = std::min(
                    DataNs::BLOCK_SIZE, geometry.canvasSize.height - blockTop);
                int blockLeft = 0;
                while (blockLeft < geometry.canvasSize.width) {
                    const int blockWidth = std::min(
                        DataNs::BLOCK_SIZE, geometry.canvasSize.width - blockLeft);
                    const cv::Rect block(blockLeft, blockTop, blockWidth, blockHeight);
                    const cv::Mat owners = buildBlockOwners(geometry, cuts, block);
                    appendBlockTerritories(geometry, owners, block, territories);
                    blockLeft += blockWidth;
                }
                blockTop += blockHeight;
            }
        }

    }

    // L'association utilise les identifiants, jamais le nom des fichiers.
    // La liste d'origine n'est remplacée qu'après validation de toutes les tuiles.
    bool orderTilePositions(const DataNs::TileList& tiles,
        std::vector<DataNs::TilePositionData>& positions,
        std::string& error) {
        error.clear();
        if (positions.size() != tiles.size()) {
            error = "positions: count does not match tile list";
            return false;
        }
        PositionByTileId positionByTileId;
        if (!indexPositions(positions, positionByTileId, error)) return false;
        std::vector<DataNs::TilePositionData> orderedPositions;
        orderedPositions.reserve(tiles.size());
        for (const auto& tile : tiles) {
            if (!tile) {
                error = "positions: null tile";
                return false;
            }
            const auto entry = positionByTileId.find(tile->getId());
            if (entry == positionByTileId.end()) {
                error = "positions: missing or repeated tile id " + std::to_string(tile->getId());
                return false;
            }
            orderedPositions.push_back(*entry->second);
            positionByTileId.erase(entry);
        }
        positions = std::move(orderedPositions);
        return true;
    }
    // Les appels directs au constructeur de territoires bénéficient du même
    // contrôle des identifiants que les appels effectués depuis le step.
    bool buildTerritories(const TerritoryInput& input,
        std::vector<DataNs::TileTerritory>& territories,
        std::string& error) {
        territories.clear();
        error.clear();
        if (!validateInput(input, error)) return false;
        auto orderedPositions = input.positions;
        if (!orderTilePositions(input.tiles, orderedPositions, error)) return false;
        const TerritoryInput orderedInput{ input.tiles, orderedPositions, input.seamlines };
        TerritoryGeometry geometry;
        PairCuts cuts;
        if (!buildGeometry(orderedInput, geometry, error) ||
            !buildPairCuts(orderedInput, geometry, cuts, error))
            return false;
        territories.resize(input.tiles.size());
        for (std::size_t tileIndex = 0; tileIndex < territories.size(); ++tileIndex)
            territories[tileIndex].tileIndex = static_cast<int>(tileIndex);
        buildAllBlocks(geometry, cuts, territories);
        return true;
    }

}