// src/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

namespace DynamicProgrammingDijkstraNs {

    SeamFinderClass::SeamFinderClass() {}

    SeamFinderClass::SeamFinderClass(CostFunction initialCostFunction, const CostMapParameters& initialParameters,
                                     SolverMethod initialMethod)
        : costFunction(initialCostFunction), parameters(initialParameters), method(initialMethod) {}

    std::vector<cv::Point> SeamFinderClass::computeSeam(const cv::Mat& overlapLeft, const cv::Mat& overlapRight) {
        if (overlapLeft.empty() || overlapRight.empty()) return {};

        // Section 5 / 5.0 : carte de coût de l'overlap (seul `treated` sert au solveur)
        const CostMaps maps = computeCost(overlapLeft, overlapRight, costFunction, parameters);

        // Section 6 : méthode choisie, extrémités libres
        return solveEdge(maps.treated, method, nullptr, nullptr, corridorRadius).path;
    }

}
