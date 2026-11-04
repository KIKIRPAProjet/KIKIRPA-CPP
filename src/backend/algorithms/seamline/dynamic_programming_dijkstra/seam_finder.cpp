
// src/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

namespace DynamicProgrammingDijkstraNs {

    SeamFinderClass::SeamFinderClass() = default;

    SeamFinderClass::SeamFinderClass(
        CostFunction costFunction,
        const CostMapParams& costMapParams,
        Method method
    )
        : costFunction(costFunction),
          params(costMapParams),
          method(method) {
    }

    std::vector<cv::Point> SeamFinderClass::compute(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage
    ) {
        if (leftImage.empty() || rightImage.empty()) {
            return {};
        }

        // Seule la carte de coût traitée est utilisée pour la résolution.
        const CostMaps costMaps = computeCost(
            leftImage,
            rightImage,
            costFunction,
            params
        );

        // Les extrémités du chemin restent libres.
        return solveEdge(
            costMaps.treated,
            method,
            nullptr,
            nullptr,
            corridorRadius
        ).path;
    }

}  // namespace DynamicProgrammingDijkstraNs
