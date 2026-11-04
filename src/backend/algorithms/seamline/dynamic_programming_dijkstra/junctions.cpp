// src/backend/algorithms/seamline/dynamic_programming_dijkstra/junctions.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/junctions.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        // (clé, side) → carte de distances
        using DistanceCache = std::map<std::pair<std::string, std::string>, cv::Mat>;

        double computeSecondsSince(std::chrono::steady_clock::time_point startTime) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        }

        // int(round(x)) Python : arrondi au pair le plus proche (mode d'arrondi par défaut).
        int roundLikePython(double value) {
            return static_cast<int>(std::nearbyint(value));
        }

        const EdgeCost& getEdge(const SeamGrid& grid, const std::string& key) {
            const auto edge = grid.edges.find(key);
            if (edge == grid.edges.end()) throw std::invalid_argument("Missing adjacency: " + key);
            if (edge->second.cost.empty()) throw std::invalid_argument("Empty cost map: " + key);
            return edge->second;
        }

        const Junction& getJunction(const JunctionSet& junctions, int row, int column) {
            const auto junction = junctions.points.find({row, column});
            if (junction == junctions.points.end())
                throw std::invalid_argument("Missing junction: J_" + std::to_string(row) + "_" + std::to_string(column));
            return junction->second;
        }

        // Équivalent de _sample_cost : arrondi puis écrêtage aux bords.
        double sampleValue(const cv::Mat& values, double row, double column) {
            const int roundedRow = std::clamp(roundLikePython(row), 0, values.rows - 1);
            const int roundedColumn = std::clamp(roundLikePython(column), 0, values.cols - 1);
            return values.at<double>(roundedRow, roundedColumn);
        }

        const cv::Mat& getCachedDistance(const SeamGrid& grid, const std::string& key, const std::string& side,
                                         SolverMethod method, DistanceCache& cache) {
            const auto cacheKey = std::make_pair(key, side);
            auto cachedDistance = cache.find(cacheKey);
            if (cachedDistance == cache.end())
                cachedDistance = cache.emplace(cacheKey, computeDirectionalDistance(getEdge(grid, key).cost, side, method)).first;
            return cachedDistance->second;
        }

        // Équivalent de _block_overlap_px : (hauteur, largeur) physiques du rectangle commun du bloc.
        std::pair<double, double> computeBlockOverlapPixels(const SeamGrid& grid, int row, int column) {
            const double horizontalOverlap = (getEdge(grid, buildEdgeKey('H', row, column)).overlapPixels +
                                              getEdge(grid, buildEdgeKey('H', row + 1, column)).overlapPixels) / 2.0;
            const double verticalOverlap = (getEdge(grid, buildEdgeKey('V', row, column)).overlapPixels +
                                            getEdge(grid, buildEdgeKey('V', row, column + 1)).overlapPixels) / 2.0;
            return {verticalOverlap, horizontalOverlap};
        }

        int computeDepthInSolver(const EdgeCost& edge, double depthPixels) {
            return std::max(2, std::min(edge.cost.rows, roundLikePython(depthPixels * edge.scale)));
        }

        Junction chooseJunctionWithCache(const SeamGrid& grid, int row, int column, SolverMethod method, int gridResolution,
                                         DistanceCache& cache) {
            if (gridResolution < 1) throw std::invalid_argument("gridResolution must be >= 1");
            const auto startTime = std::chrono::steady_clock::now();

            const std::string keyAB = buildEdgeKey('H', row, column), keyCD = buildEdgeKey('H', row + 1, column);
            const std::string keyAC = buildEdgeKey('V', row, column), keyBD = buildEdgeKey('V', row, column + 1);
            const EdgeCost &edgeAB = getEdge(grid, keyAB), &edgeCD = getEdge(grid, keyCD);
            const EdgeCost &edgeAC = getEdge(grid, keyAC), &edgeBD = getEdge(grid, keyBD);
            const cv::Mat& distanceAB = getCachedDistance(grid, keyAB, "top", method, cache);
            const cv::Mat& distanceCD = getCachedDistance(grid, keyCD, "bottom", method, cache);
            const cv::Mat& distanceAC = getCachedDistance(grid, keyAC, "top", method, cache);
            const cv::Mat& distanceBD = getCachedDistance(grid, keyBD, "bottom", method, cache);

            // Une carte de distances par seamline, depuis son bord ÉLOIGNÉ de J : AB et AC partent du
            // haut de leur carte (J est en bas / à droite), CD et BD du bas (J est en haut / à gauche).
            // Profondeur du rectangle commun exprimée dans chaque solveur.
            const auto [verticalOverlapPixels, horizontalOverlapPixels] = computeBlockOverlapPixels(grid, row, column);
            const int depthAB = computeDepthInSolver(edgeAB, verticalOverlapPixels);
            const int depthCD = computeDepthInSolver(edgeCD, verticalOverlapPixels);
            const int depthAC = computeDepthInSolver(edgeAC, horizontalOverlapPixels);
            const int depthBD = computeDepthInSolver(edgeBD, horizontalOverlapPixels);

            Junction junction;
            junction.scoreMap = cv::Mat(gridResolution, gridResolution, CV_64F, cv::Scalar(std::numeric_limits<double>::infinity()));
            const double gridStep = (gridResolution > 1) ? 1.0 / double(gridResolution - 1) : 0.0;   // np.linspace(0, 1, res)
            auto gridPosition = [&](int gridIndex) {
                return (gridResolution > 1 && gridIndex == gridResolution - 1) ? 1.0 : double(gridIndex) * gridStep;
            };

            // Recherche exhaustive sur la grille (u, v) : J = position où la somme des 4 distances
            // est minimale, c.-à-d. le point par lequel les 4 seamlines passent au moindre coût.
            for (int rowIndex = 0; rowIndex < gridResolution; ++rowIndex) {
                const double normalizedRow = gridPosition(rowIndex);
                const double columnAC = normalizedRow * (edgeAC.cost.cols - 1);   // x solveur des V = y original
                const double columnBD = normalizedRow * (edgeBD.cost.cols - 1);
                // AB proche du bas ; CD proche du haut
                const double rowAB = double(edgeAB.cost.rows - depthAB) + normalizedRow * (depthAB - 1);
                const double rowCD = normalizedRow * (depthCD - 1);
                for (int columnIndex = 0; columnIndex < gridResolution; ++columnIndex) {
                    const double normalizedColumn = gridPosition(columnIndex);
                    const double columnAB = normalizedColumn * (edgeAB.cost.cols - 1);
                    const double columnCD = normalizedColumn * (edgeCD.cost.cols - 1);
                    // AC proche de la droite ; BD de la gauche
                    const double rowAC = double(edgeAC.cost.rows - depthAC) + normalizedColumn * (depthAC - 1);
                    const double rowBD = normalizedColumn * (depthBD - 1);
                    const double score = sampleValue(distanceAB, rowAB, columnAB) + sampleValue(distanceCD, rowCD, columnCD) +
                                         sampleValue(distanceAC, rowAC, columnAC) + sampleValue(distanceBD, rowBD, columnBD);
                    junction.scoreMap.at<double>(rowIndex, columnIndex) = score;
                    if (score < junction.score) {
                        junction.score = score;
                        junction.normalizedRow = normalizedRow;
                        junction.normalizedColumn = normalizedColumn;
                    }
                }
            }
            junction.timeSeconds = computeSecondsSince(startTime);
            return junction;
        }

        // "H_2_0" → ('H', 2, 0).
        void parseEdgeKey(const std::string& key, char& direction, int& row, int& column) {
            const size_t firstSeparator = key.find('_'), secondSeparator = key.find('_', firstSeparator + 1);
            if (key.size() < 5 || (key[0] != 'H' && key[0] != 'V') || firstSeparator != 1 || secondSeparator == std::string::npos)
                throw std::invalid_argument("Invalid adjacency key: " + key);
            direction = key[0];
            row = std::stoi(key.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1));
            column = std::stoi(key.substr(secondSeparator + 1));
        }

        // Équivalent de _edge_depth_px : profondeur (px d'origine) de l'overlap perpendiculaire à l'adjacence.
        double computeEdgeDepthPixels(const SeamGrid& grid, char direction, int row, int column, bool isStartSide) {
            if (direction == 'H') {
                const int neighbourRow = isStartSide ? row - 1 : row;
                return (getEdge(grid, buildEdgeKey('V', neighbourRow, column)).overlapPixels +
                        getEdge(grid, buildEdgeKey('V', neighbourRow, column + 1)).overlapPixels) / 2.0;
            }
            const int neighbourColumn = isStartSide ? column - 1 : column;
            return (getEdge(grid, buildEdgeKey('H', row, neighbourColumn)).overlapPixels +
                    getEdge(grid, buildEdgeKey('H', row + 1, neighbourColumn)).overlapPixels) / 2.0;
        }

    }

    std::string buildEdgeKey(char direction, int row, int column) {
        return std::string(1, direction) + "_" + std::to_string(row) + "_" + std::to_string(column);
    }

    cv::Mat computeDirectionalDistance(const cv::Mat& cost, const std::string& side, SolverMethod method) {
        if (side != "top" && side != "bottom") throw std::invalid_argument("side must be 'top' or 'bottom'");
        cv::Mat orientedCost;   // pas « orientedCost = cost » : flip écrirait alors dans la carte de l'appelant
        if (side == "bottom")
            cv::flip(cost, orientedCost, 0);
        else
            orientedCost = cost;
        cv::Mat distances = (method == SolverMethod::Dijkstra8) ? computeDijkstraDistanceMap(orientedCost)
                                                                : computeDynamicProgrammingDistanceFromTop(orientedCost);
        if (side == "bottom") cv::flip(distances, distances, 0);
        return distances;
    }

    Junction chooseJunctionPoint(const SeamGrid& grid, int row, int column, SolverMethod method, int gridResolution) {
        DistanceCache cache;
        return chooseJunctionWithCache(grid, row, column, method, gridResolution, cache);
    }

    JunctionSet chooseAllJunctions(const SeamGrid& grid, SolverMethod method, int gridResolution) {
        const auto startTime = std::chrono::steady_clock::now();
        JunctionSet junctions;
        junctions.method = method;
        // Une carte (arête, côté) n'est utilisée que par un seul bloc (H_k sert en "top" au bloc k et en
        // "bottom" au bloc k−1) : le cache est donc vidé à chaque bloc, sinon toutes les cartes restent en mémoire.
        for (int row = 0; row < grid.rowCount - 1; ++row)
            for (int column = 0; column < grid.columnCount - 1; ++column) {
                DistanceCache cache;
                junctions.points[{row, column}] = chooseJunctionWithCache(grid, row, column, method, gridResolution, cache);
            }
        junctions.totalTimeSeconds = computeSecondsSince(startTime);
        return junctions;
    }

    Endpoints computeEdgeEndpoints(const SeamGrid& grid, const std::string& key, const JunctionSet& junctions) {
        char direction = 'H';
        int row = 0, column = 0;
        parseEdgeKey(key, direction, row, column);
        const EdgeCost& edge = getEdge(grid, key);
        const int height = edge.cost.rows, width = edge.cost.cols;
        auto depthOf = [&](bool isStartSide) {
            return computeDepthInSolver(edge, computeEdgeDepthPixels(grid, direction, row, column, isStartSide));
        };

        // J normalisé → pixel de CETTE carte : v (ou u pour V) le long de la largeur, u (ou v) dans
        // la bande de profondeur d située en haut (départ) ou en bas (arrivée) de la carte.
        Endpoints endpoints;
        if (direction == 'H') {
            if (row > 0) {
                const Junction& junction = getJunction(junctions, row - 1, column);
                endpoints.start = cv::Point(roundLikePython(junction.normalizedColumn * (width - 1)),
                                            roundLikePython(junction.normalizedRow * (depthOf(true) - 1)));
            }
            if (row < grid.rowCount - 1) {
                const Junction& junction = getJunction(junctions, row, column);
                const int depth = depthOf(false);
                endpoints.goal = cv::Point(roundLikePython(junction.normalizedColumn * (width - 1)),
                                           roundLikePython(double(height - depth) + junction.normalizedRow * (depth - 1)));
            }
        } else {
            // Solveur transposé : y = x original ; x = y original dans l'overlap vertical.
            if (column > 0) {
                const Junction& junction = getJunction(junctions, row, column - 1);
                endpoints.start = cv::Point(roundLikePython(junction.normalizedRow * (width - 1)),
                                            roundLikePython(junction.normalizedColumn * (depthOf(true) - 1)));
            }
            if (column < grid.columnCount - 1) {
                const Junction& junction = getJunction(junctions, row, column);
                const int depth = depthOf(false);
                endpoints.goal = cv::Point(roundLikePython(junction.normalizedRow * (width - 1)),
                                           roundLikePython(double(height - depth) + junction.normalizedColumn * (depth - 1)));
            }
        }
        return endpoints;
    }

    std::map<std::string, SolveResult> solveGlobalNetwork(const SeamGrid& grid, SolverMethod method,
                                                          const JunctionSet& junctions, int corridorRadius) {
        std::map<std::string, SolveResult> seams;
        // Chaque seamline est indépendante une fois ses extrémités fixées par les jonctions.
        for (const auto& [key, edge] : grid.edges) {
            const Endpoints endpoints = computeEdgeEndpoints(grid, key, junctions);
            seams[key] = solveEdge(edge.cost, method, endpoints.getStartPointer(), endpoints.getGoalPointer(), corridorRadius);
        }
        return seams;
    }

}
