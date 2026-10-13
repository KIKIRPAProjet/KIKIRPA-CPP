#include "seamlib/junctions.hpp"

#include "seamlib/dijkstra.hpp"
#include "seamlib/dynamic_programming.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace seamlib {

    namespace {

        using Cache = std::map<std::pair<std::string, std::string>, cv::Mat>;   // (clé, side) → carte de distances

        double seconds_since(std::chrono::steady_clock::time_point t0) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }

        // int(round(x)) Python : arrondi au pair le plus proche (mode d'arrondi par défaut).
        int py_round(double x) {
            return static_cast<int>(std::nearbyint(x));
        }

        const EdgeCost& edge_at(const SeamGrid& grid, const std::string& key) {
            const auto it = grid.edges.find(key);
            if (it == grid.edges.end()) throw std::invalid_argument("Adjacence manquante : " + key);
            if (it->second.cost.empty()) throw std::invalid_argument("Carte de coût vide : " + key);
            return it->second;
        }

        const Junction& junction_at(const JunctionSet& js, int r, int c) {
            const auto it = js.points.find({ r, c });
            if (it == js.points.end())
                throw std::invalid_argument("Jonction manquante : J_" + std::to_string(r) + "_" + std::to_string(c));
            return it->second;
        }

        // Équivalent de _sample_cost : arrondi puis écrêtage aux bords.
        double sample(const cv::Mat& arr, double y, double x) {
            const int yi = std::clamp(py_round(y), 0, arr.rows - 1);
            const int xi = std::clamp(py_round(x), 0, arr.cols - 1);
            return arr.at<double>(yi, xi);
        }

        const cv::Mat& cached_dist(const SeamGrid& grid, const std::string& key, const std::string& side, Method method,
            Cache& cache) {
            const auto ck = std::make_pair(key, side);
            auto it = cache.find(ck);
            if (it == cache.end()) it = cache.emplace(ck, directional_dist(edge_at(grid, key).cost, side, method)).first;
            return it->second;
        }

        // Équivalent de _block_overlap_px : (hauteur, largeur) physiques du rectangle commun du bloc.
        std::pair<double, double> block_overlap_px(const SeamGrid& grid, int r, int c) {
            const double hx = (edge_at(grid, edge_key('H', r, c)).overlap_px + edge_at(grid, edge_key('H', r + 1, c)).overlap_px) / 2.0;
            const double vy = (edge_at(grid, edge_key('V', r, c)).overlap_px + edge_at(grid, edge_key('V', r, c + 1)).overlap_px) / 2.0;
            return { vy, hx };
        }

        int depth_in(const EdgeCost& e, double px) {
            return std::max(2, std::min(e.cost.rows, py_round(px * e.scale)));
        }

        Junction choose_junction_cached(const SeamGrid& grid, int r, int c, Method method, int res, Cache& cache) {
            if (res < 1) throw std::invalid_argument("grid_res doit être >= 1");
            const auto t0 = std::chrono::steady_clock::now();

            const std::string kab = edge_key('H', r, c), kcd = edge_key('H', r + 1, c);
            const std::string kac = edge_key('V', r, c), kbd = edge_key('V', r, c + 1);
            const EdgeCost &AB = edge_at(grid, kab), &CD = edge_at(grid, kcd);
            const EdgeCost &AC = edge_at(grid, kac), &BD = edge_at(grid, kbd);
            const cv::Mat& dAB = cached_dist(grid, kab, "top", method, cache);
            const cv::Mat& dCD = cached_dist(grid, kcd, "bottom", method, cache);
            const cv::Mat& dAC = cached_dist(grid, kac, "top", method, cache);
            const cv::Mat& dBD = cached_dist(grid, kbd, "bottom", method, cache);

            // Une carte de distances par seamline, depuis son bord ÉLOIGNÉ de J : AB et AC partent du
            // haut de leur carte (J est en bas / à droite), CD et BD du bas (J est en haut / à gauche).
            // Profondeur du rectangle commun exprimée dans chaque solveur.
            const auto [vy_px, hx_px] = block_overlap_px(grid, r, c);
            const int ab_vdepth = depth_in(AB, vy_px), cd_vdepth = depth_in(CD, vy_px);
            const int ac_hdepth = depth_in(AC, hx_px), bd_hdepth = depth_in(BD, hx_px);

            Junction J;
            J.score_map = cv::Mat(res, res, CV_64F, cv::Scalar(std::numeric_limits<double>::infinity()));
            const double step = (res > 1) ? 1.0 / double(res - 1) : 0.0;   // np.linspace(0, 1, res)
            auto lin = [&](int i) { return (res > 1 && i == res - 1) ? 1.0 : double(i) * step; };

            // Recherche exhaustive sur la grille (u, v) : J = position où la somme des 4 distances
            // est minimale, c.-à-d. le point par lequel les 4 seamlines passent au moindre coût.
            for (int i = 0; i < res; ++i) {
                const double u = lin(i);
                const double x_ac = u * (AC.cost.cols - 1);                                  // x solveur des V = y original
                const double x_bd = u * (BD.cost.cols - 1);
                const double y_ab = double(AB.cost.rows - ab_vdepth) + u * (ab_vdepth - 1);  // AB proche du bas ; CD proche du haut
                const double y_cd = u * (cd_vdepth - 1);
                for (int j = 0; j < res; ++j) {
                    const double v = lin(j);
                    const double x_ab = v * (AB.cost.cols - 1);
                    const double x_cd = v * (CD.cost.cols - 1);
                    const double y_ac = double(AC.cost.rows - ac_hdepth) + v * (ac_hdepth - 1);  // AC proche de la droite ; BD de la gauche
                    const double y_bd = v * (bd_hdepth - 1);
                    const double score = sample(dAB, y_ab, x_ab) + sample(dCD, y_cd, x_cd)
                        + sample(dAC, y_ac, x_ac) + sample(dBD, y_bd, x_bd);
                    J.score_map.at<double>(i, j) = score;
                    if (score < J.score) {
                        J.score = score;
                        J.u = u;
                        J.v = v;
                    }
                }
            }
            J.time_s = seconds_since(t0);
            return J;
        }

        // "H_2_0" → ('H', 2, 0).
        void parse_key(const std::string& key, char& dir, int& row, int& col) {
            const size_t a = key.find('_'), b = key.find('_', a + 1);
            if (key.size() < 5 || (key[0] != 'H' && key[0] != 'V') || a != 1 || b == std::string::npos)
                throw std::invalid_argument("Clé d'adjacence invalide : " + key);
            dir = key[0];
            row = std::stoi(key.substr(a + 1, b - a - 1));
            col = std::stoi(key.substr(b + 1));
        }

        // Équivalent de _edge_depth_px : profondeur (px d'origine) de l'overlap perpendiculaire à l'adjacence.
        double edge_depth_px(const SeamGrid& grid, char dir, int r, int c, bool start_side) {
            if (dir == 'H') {
                const int rr = start_side ? r - 1 : r;
                return (edge_at(grid, edge_key('V', rr, c)).overlap_px + edge_at(grid, edge_key('V', rr, c + 1)).overlap_px) / 2.0;
            }
            const int cc = start_side ? c - 1 : c;
            return (edge_at(grid, edge_key('H', r, cc)).overlap_px + edge_at(grid, edge_key('H', r + 1, cc)).overlap_px) / 2.0;
        }

    }  // namespace

    std::string edge_key(char direction, int row, int col) {
        return std::string(1, direction) + "_" + std::to_string(row) + "_" + std::to_string(col);
    }

    cv::Mat directional_dist(const cv::Mat& cost, const std::string& side, Method method) {
        if (side != "top" && side != "bottom") throw std::invalid_argument("side doit valoir 'top' ou 'bottom'");
        cv::Mat c;   // pas « c = cost » : flip écrirait alors dans la carte de l'appelant
        if (side == "bottom")
            cv::flip(cost, c, 0);
        else
            c = cost;
        cv::Mat d = (method == Method::Dijkstra8) ? dijkstra_distance_map(c) : dp_distance_top(c);
        if (side == "bottom") cv::flip(d, d, 0);
        return d;
    }

    Junction choose_junction_point(const SeamGrid& grid, int r, int c, Method method, int grid_res) {
        Cache cache;
        return choose_junction_cached(grid, r, c, method, grid_res, cache);
    }

    JunctionSet choose_all_junctions(const SeamGrid& grid, Method method, int grid_res) {
        const auto t0 = std::chrono::steady_clock::now();
        JunctionSet js;
        js.method = method;
        Cache cache;
        for (int r = 0; r < grid.n_rows - 1; ++r)
            for (int c = 0; c < grid.n_cols - 1; ++c)
                js.points[{ r, c }] = choose_junction_cached(grid, r, c, method, grid_res, cache);
        js.total_time_s = seconds_since(t0);
        return js;
    }

    Endpoints edge_endpoints(const SeamGrid& grid, const std::string& key, const JunctionSet& junctions) {
        char dir = 'H';
        int r = 0, c = 0;
        parse_key(key, dir, r, c);
        const EdgeCost& e = edge_at(grid, key);
        const int h = e.cost.rows, w = e.cost.cols;
        auto depth = [&](bool start_side) { return depth_in(e, edge_depth_px(grid, dir, r, c, start_side)); };

        // J normalisé → pixel de CETTE carte : v (ou u pour V) le long de la largeur, u (ou v) dans
        // la bande de profondeur d située en haut (départ) ou en bas (arrivée) de la carte.
        Endpoints out;
        if (dir == 'H') {
            if (r > 0) {
                const Junction& J = junction_at(junctions, r - 1, c);
                out.start = cv::Point(py_round(J.v * (w - 1)), py_round(J.u * (depth(true) - 1)));
            }
            if (r < grid.n_rows - 1) {
                const Junction& J = junction_at(junctions, r, c);
                const int d = depth(false);
                out.goal = cv::Point(py_round(J.v * (w - 1)), py_round(double(h - d) + J.u * (d - 1)));
            }
        }
        else {
            // Solveur transposé : y = x original ; x = y original dans l'overlap vertical.
            if (c > 0) {
                const Junction& J = junction_at(junctions, r, c - 1);
                out.start = cv::Point(py_round(J.u * (w - 1)), py_round(J.v * (depth(true) - 1)));
            }
            if (c < grid.n_cols - 1) {
                const Junction& J = junction_at(junctions, r, c);
                const int d = depth(false);
                out.goal = cv::Point(py_round(J.u * (w - 1)), py_round(double(h - d) + J.v * (d - 1)));
            }
        }
        return out;
    }

    std::map<std::string, SolveResult> solve_global_network(const SeamGrid& grid, Method method, const JunctionSet& junctions,
        int corridor_radius) {
        std::map<std::string, SolveResult> out;
        // Chaque seamline est indépendante une fois ses extrémités fixées par les jonctions.
        for (const auto& [key, e] : grid.edges) {
            const Endpoints ep = edge_endpoints(grid, key, junctions);
            out[key] = solve_edge(e.cost, method, ep.start_ptr(), ep.goal_ptr(), corridor_radius);
        }
        return out;
    }

}  // namespace seamlib
