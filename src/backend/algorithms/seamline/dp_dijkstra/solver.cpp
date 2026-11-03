#include "algorithms/seamline/dp_dijkstra/solver.hpp"

#include "algorithms/seamline/dp_dijkstra/dijkstra.hpp"
#include "algorithms/seamline/dp_dijkstra/dynamic_programming.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

namespace seamlib {

    namespace {
        //Durée écoulée depuis t0, en secondes
        double seconds_since(std::chrono::steady_clock::time_point t0) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }

    }  // namespace

    // Noms pour identifier les méthodes dans les paramètres et résultats
    std::string method_name(Method m) {
        switch (m) {
        case Method::DP: return "DP";
        case Method::Dijkstra8: return "Dijkstra-8";
        case Method::DPDijkstraLocal: return "DP+Dijkstra-local";
        }
        throw std::invalid_argument("Méthode inconnue");
    }

    const std::vector<Method>& all_methods() {
        static const std::vector<Method> all = { Method::DP, Method::Dijkstra8, Method::DPDijkstraLocal };
        return all;
    }
    //Retrouver une méthode à partir de son nom exact
    Method method_from_name(const std::string& name) {
        for (Method m : all_methods())
            if (method_name(m) == name) return m;
        throw std::invalid_argument("Méthode inconnue : " + name);
    }

    //Evaluer un chemin : coût du premier pixel, puis poids de chaque déplacement
    //Un chemin vide reçoit un coût infini
    double path_objective(const cv::Mat& cost_in, const std::vector<cv::Point>& path) {
        if (path.empty()) return std::numeric_limits<double>::infinity();
        cv::Mat cost;
        cost_in.convertTo(cost, CV_64F);
        double total = cost.at<double>(path[0]);
        for (size_t i = 0; i + 1 < path.size(); ++i)
            total += edge_weight(cost, path[i].y, path[i].x, path[i + 1].y, path[i + 1].x);
        return total;
    }

    //Estimation de la mémoire des tableaux principaux et du tas de Dijkstra
    //Résultat en Mio, pas une mesure de la mémoire réelle
    double estimate_solver_memory_mb(Method method, const cv::Size& size, long long max_heap) {
        const double n = double(size.area());
        //DP : coût et distance en float64 et parents en int32
        const double dp_bytes = n * (8 + 8 + 4);   
        // Dijkstra : coût, distances, parents int64, deux masques, et estimation de 16 octets par entrée du tas
        const double dij_bytes = n * (8 + 8 + 8 + 1 + 1) + double(max_heap) * 16;  
        const double mb = 1024.0 * 1024.0;
        if (method == Method::DP) return dp_bytes / mb;
        if (method == Method::Dijkstra8) return dij_bytes / mb;
        // Modèle hybride : retenir le maximum des deux estimations, les tableaux de travail de la DP étant libérés avant Dijkstra
        return std::max(dp_bytes, dij_bytes) / mb;                                 
    }

    // Chercher un chemin dans la carte de coût avec la méthode demandée 
    // start et goal sont facultatifs : un pointeur nul laisse l'extrémité libre
    // En mode hybride, corridor_radius règle la largeur de la zone de recherche autour du chemin initial obtenu par DP

    SolveResult solve_edge(const cv::Mat& cost, Method method, const cv::Point* start, const cv::Point* goal,
        int corridor_radius) {
        SolveResult out;
        out.method = method;
        PathResult res;

        // La DP avance d'une ligne par pas et d'au plus une colonne latéralement.
        // Si les extrémités imposées rendent ce déplacement impossible, utiliser Dijkstra sur toute la carte
        const bool dp_infeasible = start && goal && std::abs(goal->x - start->x) > goal->y - start->y;

        const auto t0 = std::chrono::steady_clock::now();
        switch (method) {
        case Method::DP:
            res = dp_infeasible ? dijkstra_path(cost, start, goal) : dp_constrained(cost, start, goal);
            break;
        case Method::Dijkstra8:
            res = dijkstra_path(cost, start, goal);
            break;
        case Method::DPDijkstraLocal: {
            //Sans chemin initial réalisable par DP, aucun corridor n'est construit
            if (dp_infeasible) {                        
                const auto t_refine = std::chrono::steady_clock::now();
                res = dijkstra_path(cost, start, goal);
                out.refine_s = seconds_since(t_refine);
                out.corridor_ratio = 1.0;
                break;
            }
            // Obtenir un premier chemin par DP
            const auto t_seed = std::chrono::steady_clock::now();
            const PathResult seed = dp_constrained(cost, start, goal);
            out.seed_s = seconds_since(t_seed);
            // Limiter Dijkstra à un corridor autour de ce chemin
            //Temps de raffinement inclut la construction du corridor
            const auto t_refine = std::chrono::steady_clock::now();
            const cv::Mat allowed = hybrid_corridor(seed.path, cost.size(), corridor_radius, start, goal);
            res = dijkstra_path(cost, start, goal, allowed);
            out.refine_s = seconds_since(t_refine);
            // Part des pixels de la carte autorisés dans le corridor
            out.corridor_ratio = double(cv::countNonZero(allowed)) / double(allowed.total());
            break;
        }
        default:
            throw std::invalid_argument("Méthode inconnue");
        }
        out.wall_s = seconds_since(t0);
        //Regrouper le chemin, son coût et les statistiques de recherche
        // "method" conserve la méthode demandée, même après un repli sur Dijkstra
        out.path = std::move(res.path);
        out.objective = res.objective;
        out.expanded = res.expanded;
        out.pushes = res.pushes;
        out.max_heap = res.max_heap;
        out.cells = static_cast<long long>(cost.total());
        out.mem_est_mb = estimate_solver_memory_mb(method, cost.size(), res.max_heap);
        return out;
    }

}  // namespace seamlib
