#include "algorithms/seamline/solver.hpp"

#include "algorithms/seamline/dijkstra.hpp"
#include "algorithms/seamline/dynamic_programming.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

namespace seamlib {

    namespace {

        double seconds_since(std::chrono::steady_clock::time_point t0) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }

    }  // namespace

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

    Method method_from_name(const std::string& name) {
        for (Method m : all_methods())
            if (method_name(m) == name) return m;
        throw std::invalid_argument("Méthode inconnue : " + name);
    }

    double path_objective(const cv::Mat& cost_in, const std::vector<cv::Point>& path) {
        if (path.empty()) return std::numeric_limits<double>::infinity();
        cv::Mat cost;
        cost_in.convertTo(cost, CV_64F);
        double total = cost.at<double>(path[0]);
        for (size_t i = 0; i + 1 < path.size(); ++i)
            total += edge_weight(cost, path[i].y, path[i].x, path[i + 1].y, path[i + 1].x);
        return total;
    }

    double estimate_solver_memory_mb(Method method, const cv::Size& size, long long max_heap) {
        const double n = double(size.area());
        // Tableaux de taille h × w alloués par chaque solveur (le chemin lui-même est négligeable).
        const double dp_bytes = n * (8 + 8 + 4);                                  // coût f64 + dist f64 + parent int32
        const double dij_bytes = n * (8 + 8 + 8 + 1 + 1) + double(max_heap) * 16;  // coût, dist, parent int64, settled, allowed + tas
        const double mb = 1024.0 * 1024.0;
        if (method == Method::DP) return dp_bytes / mb;
        if (method == Method::Dijkstra8) return dij_bytes / mb;
        return std::max(dp_bytes, dij_bytes) / mb;                                 // hybride : la DP est libérée avant Dijkstra
    }

    SolveResult solve_edge(const cv::Mat& cost, Method method, const cv::Point* start, const cv::Point* goal,
        int corridor_radius) {
        SolveResult out;
        out.method = method;
        PathResult res;

        // La DP descend d'une ligne par pas avec ±1 colonne : si les deux extrémités sont imposées et que
        // l'écart latéral dépasse la hauteur disponible, aucun chemin DP n'existe → repli sur Dijkstra-8.
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
            if (dp_infeasible) {                        // pas de graine DP : Dijkstra-8 sur toute la zone
                const auto t_refine = std::chrono::steady_clock::now();
                res = dijkstra_path(cost, start, goal);
                out.refine_s = seconds_since(t_refine);
                out.corridor_ratio = 1.0;
                break;
            }
            // Même enchaînement que dp_dijkstra_local, chronométré étape par étape.
            const auto t_seed = std::chrono::steady_clock::now();
            const PathResult seed = dp_constrained(cost, start, goal);
            out.seed_s = seconds_since(t_seed);
            const auto t_refine = std::chrono::steady_clock::now();
            const cv::Mat allowed = hybrid_corridor(seed.path, cost.size(), corridor_radius, start, goal);
            res = dijkstra_path(cost, start, goal, allowed);
            out.refine_s = seconds_since(t_refine);
            out.corridor_ratio = double(cv::countNonZero(allowed)) / double(allowed.total());
            break;
        }
        default:
            throw std::invalid_argument("Méthode inconnue");
        }
        out.wall_s = seconds_since(t0);

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
