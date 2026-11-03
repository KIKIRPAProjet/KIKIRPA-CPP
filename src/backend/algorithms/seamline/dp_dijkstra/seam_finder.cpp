#include "algorithms/seamline/dp_dijkstra/seam_finder.hpp"

#include "algorithms/seamline/dp_dijkstra/cost_catalog.hpp"
#include "algorithms/seamline/dp_dijkstra/solver.hpp"

// Ici on code la méthode concrètement
namespace seamlib
{


    SeamFinder::SeamFinder()
    {

    }

    //Configuration de la fonction de coût, ses paramètres et la méthode de recherche
    SeamFinder::SeamFinder(CostFunction fn, const CostMapParams& p, Method m)
        : cost_function(fn), params(p), method(m)
    {

    }

    //Calcul d'une ligne de raccord entre deux images  de la même zone de chevauchement
    //Renvoyer les points du chemin, ou un vecteur vide si une image est vide

    std::vector<cv::Point> SeamFinder::compute(
        const cv::Mat& left,
        const cv::Mat& right
    )
    {
        if (left.empty() || right.empty())
            return {};

        // Construction de la carte de coût avec la fonction configurée
        const CostMaps maps = compute_cost(left, right, cost_function, params);

        // Recherche du chemin avec la méthode choisie, sans imposer d'extrmités
        // Que la carte finale "treated" est utilisée par le solveur
        // corridor_radius est utilisé dans la méthode hybride DP+Dijkstra
        return solve_edge(maps.treated, method, nullptr, nullptr, corridor_radius).path;
    }


}
