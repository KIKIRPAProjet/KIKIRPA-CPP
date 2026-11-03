// src/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        double computeSecondsSince(std::chrono::steady_clock::time_point startTime) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
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

    SolveResult solveEdge(const cv::Mat& cost, SolverMethod method, const cv::Point* start, const cv::Point* goal,
                          int corridorRadius) {
        SolveResult output;
        output.method = method;
        PathResult result;

        // La DP descend d'une ligne par pas avec ±1 colonne : si les deux extrémités sont imposées et que
        // l'écart latéral dépasse la hauteur disponible, aucun chemin DP n'existe → repli sur Dijkstra-8.
        const bool isDynamicProgrammingInfeasible = start && goal && std::abs(goal->x - start->x) > goal->y - start->y;

        const auto startTime = std::chrono::steady_clock::now();
        switch (method) {
            case SolverMethod::DynamicProgramming:
                result = isDynamicProgrammingInfeasible ? findDijkstraPath(cost, start, goal)
                                                        : findDynamicProgrammingPath(cost, start, goal);
                break;
            case SolverMethod::Dijkstra8:
                result = findDijkstraPath(cost, start, goal);
                break;
            case SolverMethod::DynamicProgrammingDijkstraLocal: {
                if (isDynamicProgrammingInfeasible) {   // pas de graine DP : Dijkstra-8 sur toute la zone
                    const auto refineStartTime = std::chrono::steady_clock::now();
                    result = findDijkstraPath(cost, start, goal);
                    output.refineSeconds = computeSecondsSince(refineStartTime);
                    output.corridorRatio = 1.0;
                    break;
                }
                // Même enchaînement que findDynamicProgrammingDijkstraLocalPath, chronométré étape par étape.
                const auto seedStartTime = std::chrono::steady_clock::now();
                const PathResult seed = findDynamicProgrammingPath(cost, start, goal);
                output.seedSeconds = computeSecondsSince(seedStartTime);
                const auto refineStartTime = std::chrono::steady_clock::now();
                const cv::Mat allowedMask = buildHybridCorridor(seed.path, cost.size(), corridorRadius, start, goal);
                result = findDijkstraPath(cost, start, goal, allowedMask);
                output.refineSeconds = computeSecondsSince(refineStartTime);
                output.corridorRatio = double(cv::countNonZero(allowedMask)) / double(allowedMask.total());
                break;
            }
            default:
                throw std::invalid_argument("Unknown method");
        }
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
