#include "algorithms/seamline/dp_dijkstra/dijkstra.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace seamlib {

    namespace {

        const double INF = std::numeric_limits<double>::infinity();

        // Même ordre que NEIGHBORS en Python : (dy, dx).
        const int NEIGHBORS[8][2] = { {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1} };

        struct DijkstraOut {
            cv::Mat dist;                     // CV_64F
            std::vector<long long> parent;    // indice y * w + x du prédécesseur, -1 sinon
            long long goal_node = -1;
            long long expanded = 0, pushes = 0, max_heap = 0;
            cv::Mat settled;                  // CV_8U
        };

        void check_inside(const cv::Point* p, const cv::Mat& cost, const char* name) {
            if (p && !cv::Rect(0, 0, cost.cols, cost.rows).contains(*p))
                throw std::invalid_argument(std::string("Dijkstra : ") + name + " hors de la carte de coût");
        }

        // Équivalent de _dijkstra_core. exhaustive = true : on n'arrête pas à l'objectif (carte complète).
        DijkstraOut dijkstra_core(const cv::Mat& cost_in, const cv::Point* start, const cv::Point* goal,
            const cv::Mat& allowed, bool exhaustive) {
            CV_Assert(cost_in.channels() == 1 && !cost_in.empty());
            cv::Mat cost;
            cost_in.convertTo(cost, CV_64F);
            const int h = cost.rows, w = cost.cols;
            const long long n = static_cast<long long>(h) * w;
            if (!allowed.empty()) CV_Assert(allowed.size() == cost.size() && allowed.type() == CV_8U);
            check_inside(start, cost, "start");
            check_inside(goal, cost, "goal");
            auto is_allowed = [&](int y, int x) { return allowed.empty() || allowed.at<uchar>(y, x) != 0; };

            // Les pixels sont numérotés node = y * w + x pour tenir dans un tableau 1D (comme le Python).
            DijkstraOut out;
            out.dist = cv::Mat(cost.size(), CV_64F, cv::Scalar(INF));
            out.parent.assign(static_cast<size_t>(n), -1);
            out.settled = cv::Mat::zeros(cost.size(), CV_8U);
            double* dist = out.dist.ptr<double>();
            uchar* settled = out.settled.ptr<uchar>();

            // Tas min sur (distance, nœud) : même départage que heapq sur des tuples Python.
            using Item = std::pair<double, long long>;
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

            auto push_start = [&](int y, int x) {
                if (!is_allowed(y, x)) return;
                const long long node = static_cast<long long>(y) * w + x;
                dist[node] = cost.at<double>(y, x);
                pq.push({ dist[node], node });
                ++out.pushes;
            };
            // Départ : le point imposé, ou tous les pixels du bord haut (source multiple).
            if (start)
                push_start(start->y, start->x);
            else
                for (int x = 0; x < w; ++x) push_start(0, x);
            out.max_heap = static_cast<long long>(pq.size());

            while (!pq.empty()) {
                const auto [d, node] = pq.top();
                pq.pop();
                // Entrée périmée : le nœud a été réinséré depuis avec une distance plus courte.
                if (settled[node] || d != dist[node]) continue;
                settled[node] = 1;
                ++out.expanded;
                const int y = static_cast<int>(node / w), x = static_cast<int>(node % w);
                // Premier pixel d'arrivée fixé = le moins cher (propriété de Dijkstra). En mode
                // exhaustif, on continue pour remplir toute la carte des distances.
                if ((!goal && y == h - 1) || (goal && goal->y == y && goal->x == x)) {
                    out.goal_node = node;
                    if (!exhaustive) break;
                }
                // Relâchement des 8 voisins avec la même règle de pas que la DP.
                for (const auto& nb : NEIGHBORS) {
                    const int ny = y + nb[0], nx = x + nb[1];
                    if (ny < 0 || ny >= h || nx < 0 || nx >= w || !is_allowed(ny, nx)) continue;
                    const long long nn = static_cast<long long>(ny) * w + nx;
                    if (settled[nn]) continue;
                    const double nd = d + edge_weight(cost, y, x, ny, nx);
                    if (nd < dist[nn]) {
                        dist[nn] = nd;
                        out.parent[static_cast<size_t>(nn)] = node;
                        pq.push({ nd, nn });
                        ++out.pushes;
                        out.max_heap = std::max(out.max_heap, static_cast<long long>(pq.size()));
                    }
                }
            }
            return out;
        }

    }  // namespace

    PathResult dijkstra_path(const cv::Mat& cost, const cv::Point* start, const cv::Point* goal, const cv::Mat& allowed,
        bool keep_settled) {
        DijkstraOut d = dijkstra_core(cost, start, goal, allowed, false);
        if (d.goal_node < 0) throw std::runtime_error("Pas de chemin Dijkstra");

        // Remontée des parents depuis l'arrivée, puis inversion pour aller du départ à l'arrivée.
        PathResult res;
        const int w = cost.cols;
        for (long long node = d.goal_node; node != -1; node = d.parent[static_cast<size_t>(node)])
            res.path.push_back(cv::Point(static_cast<int>(node % w), static_cast<int>(node / w)));
        std::reverse(res.path.begin(), res.path.end());
        res.objective = d.dist.ptr<double>()[d.goal_node];
        res.expanded = d.expanded;
        res.pushes = d.pushes;
        res.max_heap = d.max_heap;
        if (keep_settled) res.settled = d.settled;
        return res;
    }

    cv::Mat dijkstra_distance_map(const cv::Mat& cost) {
        return dijkstra_core(cost, nullptr, nullptr, cv::Mat(), true).dist;
    }

    cv::Mat corridor_mask_from_path(const std::vector<cv::Point>& path, const cv::Size& size, int radius) {
        cv::Mat seed = cv::Mat::zeros(size, CV_8U);
        for (const cv::Point& p : path) seed.at<uchar>(p) = 1;
        const int k = 2 * radius + 1;
        cv::Mat mask;
        cv::dilate(seed, mask, cv::Mat::ones(k, k, CV_8U));
        return mask;
    }

    cv::Mat hybrid_corridor(const std::vector<cv::Point>& seed_path, const cv::Size& size, int radius, const cv::Point* start,
        const cv::Point* goal) {
        cv::Mat allowed = corridor_mask_from_path(seed_path, size, radius);
        if (start)
            allowed.at<uchar>(*start) = 1;
        else
            allowed.row(0).setTo(1);
        if (goal)
            allowed.at<uchar>(*goal) = 1;
        else
            allowed.row(allowed.rows - 1).setTo(1);
        return allowed;
    }

    PathResult dp_dijkstra_local(const cv::Mat& cost, int radius, const cv::Point* start, const cv::Point* goal) {
        // 1. graine DP (rapide)  2. corridor autour d'elle  3. Dijkstra exact, mais seulement dans le corridor.
        const PathResult seed = dp_constrained(cost, start, goal);
        const cv::Mat allowed = hybrid_corridor(seed.path, cost.size(), radius, start, goal);
        return dijkstra_path(cost, start, goal, allowed);
    }

}  // namespace seamlib
