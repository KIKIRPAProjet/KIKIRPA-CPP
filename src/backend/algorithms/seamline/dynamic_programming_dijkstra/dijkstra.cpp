// src/backend/algorithms/seamline/dynamic_programming_dijkstra/dijkstra.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        const double INFINITE_DISTANCE = std::numeric_limits<double>::infinity();

        // Même ordre que NEIGHBORS en Python : (dy, dx).
        const int NEIGHBOUR_OFFSETS[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

        struct DijkstraOutput {
            cv::Mat distances;                // CV_64F
            std::vector<long long> parents;   // indice row * width + column du prédécesseur, -1 sinon
            long long goalNode = -1;
            long long expandedCount = 0, pushCount = 0, maximumHeapSize = 0;
            cv::Mat settledMask;              // CV_8U
        };

        void checkInside(const cv::Point* point, const cv::Mat& cost, const char* pointName) {
            if (point && !cv::Rect(0, 0, cost.cols, cost.rows).contains(*point))
                throw std::invalid_argument(std::string("Dijkstra: ") + pointName + " is outside the cost map");
        }

        // Équivalent de _dijkstra_core. isExhaustive = true : on n'arrête pas à l'objectif (carte complète).
        DijkstraOutput runDijkstra(const cv::Mat& inputCost, const cv::Point* start, const cv::Point* goal,
                                   const cv::Mat& allowedMask, bool isExhaustive) {
            CV_Assert(inputCost.channels() == 1 && !inputCost.empty());
            cv::Mat cost;
            inputCost.convertTo(cost, CV_64F);
            const int height = cost.rows, width = cost.cols;
            const long long nodeCount = static_cast<long long>(height) * width;
            if (!allowedMask.empty()) CV_Assert(allowedMask.size() == cost.size() && allowedMask.type() == CV_8U);
            checkInside(start, cost, "start");
            checkInside(goal, cost, "goal");
            auto isAllowed = [&](int row, int column) { return allowedMask.empty() || allowedMask.at<uchar>(row, column) != 0; };

            // Les pixels sont numérotés node = row * width + column pour tenir dans un tableau 1D (comme le Python).
            DijkstraOutput output;
            output.distances = cv::Mat(cost.size(), CV_64F, cv::Scalar(INFINITE_DISTANCE));
            output.parents.assign(static_cast<size_t>(nodeCount), -1);
            output.settledMask = cv::Mat::zeros(cost.size(), CV_8U);
            double* distances = output.distances.ptr<double>();
            uchar* settled = output.settledMask.ptr<uchar>();

            // Tas min sur (distance, nœud) : même départage que heapq sur des tuples Python.
            using HeapItem = std::pair<double, long long>;
            std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> heap;

            auto pushStart = [&](int row, int column) {
                if (!isAllowed(row, column)) return;
                const long long node = static_cast<long long>(row) * width + column;
                distances[node] = cost.at<double>(row, column);
                heap.push({distances[node], node});
                ++output.pushCount;
            };
            // Départ : le point imposé, ou tous les pixels du bord haut (source multiple).
            if (start)
                pushStart(start->y, start->x);
            else
                for (int column = 0; column < width; ++column) pushStart(0, column);
            output.maximumHeapSize = static_cast<long long>(heap.size());

            while (!heap.empty()) {
                const auto [distance, node] = heap.top();
                heap.pop();
                // Entrée périmée : le nœud a été réinséré depuis avec une distance plus courte.
                if (settled[node] || distance != distances[node]) continue;
                settled[node] = 1;
                ++output.expandedCount;
                const int row = static_cast<int>(node / width), column = static_cast<int>(node % width);
                // Premier pixel d'arrivée fixé = le moins cher (propriété de Dijkstra). En mode
                // exhaustif, on continue pour remplir toute la carte des distances.
                if ((!goal && row == height - 1) || (goal && goal->y == row && goal->x == column)) {
                    output.goalNode = node;
                    if (!isExhaustive) break;
                }
                // Relâchement des 8 voisins avec la même règle de pas que la DP.
                for (const auto& neighbourOffset : NEIGHBOUR_OFFSETS) {
                    const int neighbourRow = row + neighbourOffset[0], neighbourColumn = column + neighbourOffset[1];
                    if (neighbourRow < 0 || neighbourRow >= height || neighbourColumn < 0 || neighbourColumn >= width ||
                        !isAllowed(neighbourRow, neighbourColumn))
                        continue;
                    const long long neighbourNode = static_cast<long long>(neighbourRow) * width + neighbourColumn;
                    if (settled[neighbourNode]) continue;
                    const double neighbourDistance =
                        distance + computeEdgeWeight(cost, row, column, neighbourRow, neighbourColumn);
                    if (neighbourDistance < distances[neighbourNode]) {
                        distances[neighbourNode] = neighbourDistance;
                        output.parents[static_cast<size_t>(neighbourNode)] = node;
                        heap.push({neighbourDistance, neighbourNode});
                        ++output.pushCount;
                        output.maximumHeapSize = std::max(output.maximumHeapSize, static_cast<long long>(heap.size()));
                    }
                }
            }
            return output;
        }

    }

    PathResult findDijkstraPath(const cv::Mat& cost, const cv::Point* start, const cv::Point* goal, const cv::Mat& allowedMask,
                                bool keepSettled) {
        DijkstraOutput dijkstraOutput = runDijkstra(cost, start, goal, allowedMask, false);
        if (dijkstraOutput.goalNode < 0) throw std::runtime_error("no Dijkstra path");

        // Remontée des parents depuis l'arrivée, puis inversion pour aller du départ à l'arrivée.
        PathResult result;
        const int width = cost.cols;
        for (long long node = dijkstraOutput.goalNode; node != -1; node = dijkstraOutput.parents[static_cast<size_t>(node)])
            result.path.push_back(cv::Point(static_cast<int>(node % width), static_cast<int>(node / width)));
        std::reverse(result.path.begin(), result.path.end());
        result.objective = dijkstraOutput.distances.ptr<double>()[dijkstraOutput.goalNode];
        result.expandedCount = dijkstraOutput.expandedCount;
        result.pushCount = dijkstraOutput.pushCount;
        result.maximumHeapSize = dijkstraOutput.maximumHeapSize;
        if (keepSettled) result.settledMask = dijkstraOutput.settledMask;
        return result;
    }

    cv::Mat computeDijkstraDistanceMap(const cv::Mat& cost) {
        return runDijkstra(cost, nullptr, nullptr, cv::Mat(), true).distances;
    }

    cv::Mat buildCorridorMask(const std::vector<cv::Point>& path, const cv::Size& size, int radius) {
        cv::Mat seedMask = cv::Mat::zeros(size, CV_8U);
        for (const cv::Point& point : path) seedMask.at<uchar>(point) = 1;
        const int kernelSize = 2 * radius + 1;
        cv::Mat corridorMask;
        cv::dilate(seedMask, corridorMask, cv::Mat::ones(kernelSize, kernelSize, CV_8U));
        return corridorMask;
    }

    cv::Mat buildHybridCorridor(const std::vector<cv::Point>& seedPath, const cv::Size& size, int radius, const cv::Point* start,
                                const cv::Point* goal) {
        cv::Mat allowedMask = buildCorridorMask(seedPath, size, radius);
        if (start)
            allowedMask.at<uchar>(*start) = 1;
        else
            allowedMask.row(0).setTo(1);
        if (goal)
            allowedMask.at<uchar>(*goal) = 1;
        else
            allowedMask.row(allowedMask.rows - 1).setTo(1);
        return allowedMask;
    }

    PathResult findDynamicProgrammingDijkstraLocalPath(const cv::Mat& cost, int radius, const cv::Point* start,
                                                       const cv::Point* goal) {
        // 1. graine DP (rapide)  2. corridor autour d'elle  3. Dijkstra exact, mais seulement dans le corridor.
        const PathResult seed = findDynamicProgrammingPath(cost, start, goal);
        const cv::Mat allowedMask = buildHybridCorridor(seed.path, cost.size(), radius, start, goal);
        return findDijkstraPath(cost, start, goal, allowedMask);
    }

}
