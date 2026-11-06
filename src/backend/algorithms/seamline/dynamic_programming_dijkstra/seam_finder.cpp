// src/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <vector>

namespace DynamicProgrammingDijkstraNs {

    SeamFinderClass::SeamFinderClass() = default;

    // Configure la fonction de coût, ses paramètres et la méthode de recherche du chemin.
    SeamFinderClass::SeamFinderClass(CostFunction initialCostFunction, const CostMapParameters& initialParameters,
                                     SolverMethod initialMethod)
        : costFunction(initialCostFunction), parameters(initialParameters), method(initialMethod) {}

    // Ligne de raccord entre les deux vues d'une même zone de recouvrement.
    // Renvoie les points du chemin, ou un vecteur vide si une des deux images est vide.
    std::vector<cv::Point> SeamFinderClass::computeSeam(const cv::Mat& overlapLeft, const cv::Mat& overlapRight) {
        if (overlapLeft.empty() || overlapRight.empty()) return {};

        // Section 5 / 5.0 : carte de coût construite avec la fonction configurée.
        // Seule la carte finale `treated` sert au solveur.
        const CostMaps maps = computeCost(overlapLeft, overlapRight, costFunction, parameters);

        // Section 6 : chemin avec la méthode choisie, sans imposer d'extrémités.
        // corridorRadius ne sert qu'à la méthode hybride DynamicProgrammingDijkstraLocal.
        return solveEdge(maps.treated, method, nullptr, nullptr, corridorRadius).path;
    }

}
