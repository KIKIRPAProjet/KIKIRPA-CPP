
// src/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        // Durée écoulée depuis startTime, en secondes.
        double secondsSince(std::chrono::steady_clock::time_point startTime) {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime
            ).count();
        }

    }  // namespace

    // Noms utilisés pour identifier les méthodes dans les paramètres et résultats.
    std::string getMethodName(Method method) {
        switch (method) {
            case Method::DP: return "DP";
            case Method::Dijkstra8: return "Dijkstra-8";
            case Method::DPDijkstraLocal: return "DP+Dijkstra-local";
        }
        throw std::invalid_argument("Unknown method");
    }

    const std::vector<Method>& getAllMethods() {
        static const std::vector<Method> methods = {
            Method::DP,
            Method::Dijkstra8,
            Method::DPDijkstraLocal
        };
        return methods;
    }

    // Retrouver une méthode à partir de son nom exact.
    Method getMethodFromName(const std::string& methodName) {
        for (Method method : getAllMethods()) {
            if (getMethodName(method) == methodName) {
                return method;
            }
        }
        throw std::invalid_argument("Unknown method: " + methodName);
    }

    // Évaluer un chemin : coût du premier pixel, puis poids de chaque déplacement.
    // Un chemin vide reçoit un coût infini.
    double computePathObjective(const cv::Mat& inputCost, const std::vector<cv::Point>& path) {
        if (path.empty()) {
            return std::numeric_limits<double>::infinity();
        }

        cv::Mat costMap;
        inputCost.convertTo(costMap, CV_64F);

        double totalCost = costMap.at<double>(path[0]);

        for (size_t pathIndex = 0; pathIndex + 1 < path.size(); ++pathIndex) {
            totalCost += edge_weight(
                costMap,
                path[pathIndex].y,
                path[pathIndex].x,
                path[pathIndex + 1].y,
                path[pathIndex + 1].x
            );
        }

        return totalCost;
    }

    // Estimation de la mémoire des tableaux principaux et du tas de Dijkstra.
    // Résultat en Mio, pas une mesure de la mémoire réelle.
    double estimateSolverMemoryMiB(Method method, const cv::Size& imageSize, long long maxHeap) {
        const double pixelCount = static_cast<double>(imageSize.area());

        // DP : coût et distance en float64 et parents en int32.
        const double dpBytes = pixelCount * (8 + 8 + 4);

        // Dijkstra : coût, distances, parents int64, deux masques
        // et estimation de 16 octets par entrée du tas.
        const double dijkstraBytes =
            pixelCount * (8 + 8 + 8 + 1 + 1)
            + static_cast<double>(maxHeap) * 16;

        constexpr double BYTES_PER_MIB = 1024.0 * 1024.0;

        if (method == Method::DP) {
            return dpBytes / BYTES_PER_MIB;
        }

        if (method == Method::Dijkstra8) {
            return dijkstraBytes / BYTES_PER_MIB;
        }

        // Les tableaux de travail de la DP sont libérés avant Dijkstra.
        return std::max(dpBytes, dijkstraBytes) / BYTES_PER_MIB;
    }

    // Chercher un chemin dans la carte de coût avec la méthode demandée.
    // startPoint et goalPoint sont facultatifs : nullptr laisse l'extrémité libre.
    // En mode hybride, corridorRadius règle la largeur du corridor autour du chemin DP.
    SolveResult solveEdge(
        const cv::Mat& costMap,
        Method method,
        const cv::Point* startPoint,
        const cv::Point* goalPoint,
        int corridorRadius
    ) {
        SolveResult solveResult;
        solveResult.method = method;

        PathResult pathResult;

        // La DP avance d'une ligne par pas et d'au plus une colonne latéralement.
        // Si les extrémités imposées rendent ce déplacement impossible,
        // utiliser Dijkstra sur toute la carte.
        const bool isDpInfeasible =
            startPoint && goalPoint &&
            std::abs(goalPoint->x - startPoint->x) >
            goalPoint->y - startPoint->y;

        const auto startTime = std::chrono::steady_clock::now();

        switch (method) {
            case Method::DP:
                pathResult = isDpInfeasible
                    ? dijkstra_path(costMap, startPoint, goalPoint)
                    : dp_constrained(costMap, startPoint, goalPoint);
                break;

            case Method::Dijkstra8:
                pathResult = dijkstra_path(costMap, startPoint, goalPoint);
                break;

            case Method::DPDijkstraLocal: {

                // Sans chemin initial réalisable par DP, aucun corridor n'est construit.
                if (isDpInfeasible) {
                    const auto refinementStartTime = std::chrono::steady_clock::now();

                    pathResult = dijkstra_path(costMap, startPoint, goalPoint);

                    solveResult.refine_s = secondsSince(refinementStartTime);
                    solveResult.corridor_ratio = 1.0;
                    break;
                }

                // Obtenir un premier chemin par DP.
                const auto seedStartTime = std::chrono::steady_clock::now();

                const PathResult seedPathResult =
                    dp_constrained(costMap, startPoint, goalPoint);

                solveResult.seed_s = secondsSince(seedStartTime);

                // Limiter Dijkstra à un corridor autour du chemin initial.
                // Le temps de raffinement inclut la construction du corridor.
                const auto refinementStartTime = std::chrono::steady_clock::now();

                const cv::Mat allowedCorridorMask = hybrid_corridor(
                    seedPathResult.path,
                    costMap.size(),
                    corridorRadius,
                    startPoint,
                    goalPoint
                );

                pathResult = dijkstra_path(
                    costMap,
                    startPoint,
                    goalPoint,
                    allowedCorridorMask
                );

                solveResult.refine_s = secondsSince(refinementStartTime);

                // Part des pixels autorisés dans le corridor.
                solveResult.corridor_ratio =
                    static_cast<double>(cv::countNonZero(allowedCorridorMask)) /
                    static_cast<double>(allowedCorridorMask.total());

                break;
            }

            default:
                throw std::invalid_argument("Unknown method");
        }

        solveResult.wall_s = secondsSince(startTime);

        // Regrouper le chemin, son coût et les statistiques de recherche.
        // method conserve la méthode demandée, même après un repli sur Dijkstra.
        solveResult.path = std::move(pathResult.path);
        solveResult.objective = pathResult.objective;
        solveResult.expanded = pathResult.expanded;
        solveResult.pushes = pathResult.pushes;
        solveResult.max_heap = pathResult.max_heap;
        solveResult.cells = static_cast<long long>(costMap.total());

        solveResult.mem_est_mb = estimateSolverMemoryMiB(
            method,
            costMap.size(),
            pathResult.max_heap
        );

        return solveResult;
    }

}  // namespace DynamicProgrammingDijkstraNs
