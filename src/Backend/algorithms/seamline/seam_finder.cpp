#include "algorithms/seamline/seam_finder.hpp"

#include "algorithms/seamline/cost_catalog.hpp"
#include "algorithms/seamline/solver.hpp"

// Ici on code la méthode concrètement
namespace seamlib
{


    SeamFinder::SeamFinder()
    {

    }


    SeamFinder::SeamFinder(CostFunction fn, const CostMapParams& p, Method m)
        : cost_function(fn), params(p), method(m)
    {

    }


    std::vector<cv::Point> SeamFinder::compute(
        const cv::Mat& left,
        const cv::Mat& right
    )
    {
        if (left.empty() || right.empty())
            return {};

        // Section 5 / 5.0 : carte de coût de l'overlap (seul `treated` sert au solveur)
        const CostMaps maps = compute_cost(left, right, cost_function, params);

        // Section 6 : méthode choisie, extrémités libres
        return solve_edge(maps.treated, method, nullptr, nullptr, corridor_radius).path;
    }


}
