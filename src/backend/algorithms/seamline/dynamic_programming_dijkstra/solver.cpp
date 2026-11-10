// src/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        // Extrémités et rayon reçus par solveEdge, regroupés pour les fonctions d'aide.
        struct EdgeRequest {
            const cv::Point* start = nullptr;
            const cv::Point* goal = nullptr;
            int corridorRadius = DEFAULT_CORRIDOR_RADIUS;
        };

        double computeSecondsSince(std::chrono::steady_clock::time_point startTime) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        }

        // La DP descend d'une ligne par pas avec ±1 colonne : si les deux extrémités sont imposées et que
        // l'écart latéral dépasse la hauteur disponible, aucun chemin DP n'existe.
        bool isDynamicProgrammingFeasible(const EdgeRequest& request) {
            if (request.start == nullptr || request.goal == nullptr) return true;
            return std::abs(request.goal->x - request.start->x) <= request.goal->y - request.start->y;
        }

        // DP → Dijkstra local, chronométré étape par étape (même enchaînement que
        // findDynamicProgrammingDijkstraLocalPath). Remplit seedSeconds, refineSeconds et corridorRatio.
        PathResult solveDynamicProgrammingDijkstraLocal(const cv::Mat& cost, const EdgeRequest& request,
                                                        SolveResult& output) {
            if (!isDynamicProgrammingFeasible(request)) {   // pas de graine DP : Dijkstra-8 sur toute la zone
                const auto refineStartTime = std::chrono::steady_clock::now();
                PathResult result = findDijkstraPath(cost, request.start, request.goal);
                output.refineSeconds = computeSecondsSince(refineStartTime);
                output.corridorRatio = 1.0;
                return result;
            }
            const auto seedStartTime = std::chrono::steady_clock::now();
            const PathResult seed = findDynamicProgrammingPath(cost, request.start, request.goal);
            output.seedSeconds = computeSecondsSince(seedStartTime);

            // Le temps de raffinement inclut la construction du corridor.
            const auto refineStartTime = std::chrono::steady_clock::now();
            const cv::Mat allowedMask =
                buildHybridCorridor(seed.path, cost.size(), request.corridorRadius, request.start, request.goal);
            PathResult result = findDijkstraPath(cost, request.start, request.goal, allowedMask);
            output.refineSeconds = computeSecondsSince(refineStartTime);
            output.corridorRatio = double(cv::countNonZero(allowedMask)) / double(allowedMask.total());
            return result;
        }

        // Lance la méthode demandée. Lève std::invalid_argument si elle est inconnue.
        PathResult runMethod(const cv::Mat& cost, SolverMethod method, const EdgeRequest& request, SolveResult& output) {
            switch (method) {
                case SolverMethod::DynamicProgramming:
                    // Repli sur Dijkstra-8 quand les extrémités imposées rendent la DP impossible.
                    return isDynamicProgrammingFeasible(request)
                               ? findDynamicProgrammingPath(cost, request.start, request.goal)
                               : findDijkstraPath(cost, request.start, request.goal);
                case SolverMethod::Dijkstra8:
                    return findDijkstraPath(cost, request.start, request.goal);
                case SolverMethod::DynamicProgrammingDijkstraLocal:
                    return solveDynamicProgrammingDijkstraLocal(cost, request, output);
                default:
                    throw std::invalid_argument("Unknown method");
            }
        }

    }

    std::string getMethodName(SolverMethod method) {
        switch (method) {
            case SolverMethod::DynamicProgramming: return "DP";
            case SolverMethod::Dijkstra8: return "Dijkstra-8";
            case SolverMethod::DynamicProgrammingDijkstraLocal: return "DP+Dijkstra-local";
        }
        throw std::invalid_argument("Unknown method");
    }

    const std::vector<SolverMethod>& getAllMethods() {
        static const std::vector<SolverMethod> allMethods = {SolverMethod::DynamicProgramming, SolverMethod::Dijkstra8,
                                                             SolverMethod::DynamicProgrammingDijkstraLocal};
        return allMethods;
    }

    SolverMethod parseMethodName(const std::string& name) {
        for (SolverMethod method : getAllMethods())
            if (getMethodName(method) == name) return method;
        throw std::invalid_argument("Unknown method: " + name);
    }

    // Coût du premier pixel, puis poids de chaque déplacement.
    double computePathObjective(const cv::Mat& inputCost, const std::vector<cv::Point>& path) {
        if (path.empty()) return std::numeric_limits<double>::infinity();
        cv::Mat cost;
        inputCost.convertTo(cost, CV_64F);
        double total = cost.at<double>(path[0]);
        for (size_t pointIndex = 0; pointIndex + 1 < path.size(); ++pointIndex)
            total += computeEdgeWeight(cost, path[pointIndex].y, path[pointIndex].x, path[pointIndex + 1].y,
                                       path[pointIndex + 1].x);
        return total;
    }

    // Estimation en Mio, pas une mesure de la mémoire réellement utilisée.
    double estimateSolverMemoryMegabytes(SolverMethod method, const cv::Size& size, long long maximumHeapSize) {
        const double pixelCount = double(size.area());
        // Tableaux de taille h × w alloués par chaque solveur (le chemin lui-même est négligeable).
        // DP : coût f64 + distance f64 + parent int32
        const double dynamicProgrammingBytes = pixelCount * (8 + 8 + 4);
        // Dijkstra : coût, distance, parent int64, settled, allowed + tas
        const double dijkstraBytes = pixelCount * (8 + 8 + 8 + 1 + 1) + double(maximumHeapSize) * 16;
        const double bytesPerMegabyte = 1024.0 * 1024.0;
        if (method == SolverMethod::DynamicProgramming) return dynamicProgrammingBytes / bytesPerMegabyte;
        if (method == SolverMethod::Dijkstra8) return dijkstraBytes / bytesPerMegabyte;
        // Hybride : la DP est libérée avant Dijkstra
        return std::max(dynamicProgrammingBytes, dijkstraBytes) / bytesPerMegabyte;
    }

    // 5 paramètres, ceux de solve_edge du notebook : API appelée par junctions.cpp et les tests, les 3 derniers
    // ont une valeur par défaut.
    // NOLINTNEXTLINE(readability-function-size) : nombre de paramètres seulement, voir ci-dessus
    SolveResult solveEdge(const cv::Mat& cost, SolverMethod method, const cv::Point* start, const cv::Point* goal,
                          int corridorRadius) {
        SolveResult output;
        output.method = method;   // la méthode demandée, même après un repli sur Dijkstra-8

        const auto startTime = std::chrono::steady_clock::now();
        PathResult result = runMethod(cost, method, EdgeRequest{start, goal, corridorRadius}, output);
        output.wallSeconds = computeSecondsSince(startTime);

        output.path = std::move(result.path);
        output.objective = result.objective;
        output.expandedCount = result.expandedCount;
        output.pushCount = result.pushCount;
        output.maximumHeapSize = result.maximumHeapSize;
        output.cellCount = static_cast<long long>(cost.total());
        output.estimatedMemoryMegabytes = estimateSolverMemoryMegabytes(method, cost.size(), result.maximumHeapSize);
        return output;
    }

}
