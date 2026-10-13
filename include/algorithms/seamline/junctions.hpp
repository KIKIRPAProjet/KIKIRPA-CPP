// =====================================================================================
// Section 7 · Choix des points de jonction  +  Section 8 · Réseau global de seamlines
// Conversion C++ de « directional_dist ... choose_all_junctions » et « edge_endpoints ...
// solve_global_network » du notebook projet_kik_irpa_montauban_final.
//
// Grille de n_rows × n_cols tuiles :
//   adjacence H_r_c : tuile (r, c) | tuile (r, c+1)        r ∈ [0, n_rows),     c ∈ [0, n_cols−1)
//   adjacence V_r_c : tuile (r, c) au-dessus de (r+1, c)   r ∈ [0, n_rows−1),   c ∈ [0, n_cols)
//   jonction  J_r_c : centre du bloc 2×2  A=(r,c)  B=(r,c+1)  C=(r+1,c)  D=(r+1,c+1)
//
//          A │ B          AB = H_r_c     (seamline verticale au-dessus de J)
//        ────J────        CD = H_{r+1}_c (seamline verticale au-dessous de J)
//          C │ D          AC = V_r_c     (seamline horizontale à gauche de J)
//                         BD = V_r_{c+1} (seamline horizontale à droite de J)
//
// Pour chaque bloc, J minimise la somme des QUATRE cartes de distances cumulées des seamlines
// qui s'y rejoignent. J devient ensuite l'extrémité imposée de ces quatre seamlines : le réseau
// est continu (aucune seamline ne s'arrête « dans le vide »).
//
//   Method::DP              : cartes de distances DP (dp_distance_top)
//   Method::Dijkstra8       : cartes de distances Dijkstra-8 (dijkstra_distance_map)
//   Method::DPDijkstraLocal : réutilise les jonctions DP (évite 4 gros Dijkstra par jonction)
//
// Repère solveur : comme dans le notebook, une adjacence V est TRANSPOSÉE (le solveur va toujours
// du haut vers le bas). Transposer les deux overlaps haut/bas (cv::transpose) AVANT compute_cost.
//
// Exemple :
//
//     seamlib::SeamGrid grid;
//     grid.n_rows = 5; grid.n_cols = 3;
//     grid.edges["H_0_0"] = { cost_h00, 2300.0, 0.1 };     // carte, overlap (px d'origine), échelle
//     ...                                                   // les 22 adjacences
//     seamlib::JunctionSet J = seamlib::choose_all_junctions(grid, seamlib::Method::DP);
//     std::map<std::string, seamlib::SolveResult> seams = seamlib::solve_global_network(grid, seamlib::Method::DP, J);
// =====================================================================================
#pragma once

#include <opencv2/core.hpp>

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "algorithms/seamline/export.hpp"
#include "algorithms/seamline/solver.hpp"

namespace seamlib {

    // Une adjacence telle que la voit le solveur (équivalent des champs utiles de EdgeRecord).
    struct EdgeCost {
        cv::Mat cost;              // carte de coût 1 canal (ex. CostMaps::treated) ; adjacence V : overlaps transposés
        double overlap_px = 0.0;   // largeur de l'overlap en pixels d'origine
        double scale = 1.0;        // pixels solveur / pixels d'origine (1 si la carte n'est pas réduite)
    };

    // Grille complète : dimensions + adjacences indexées par "H_r_c" / "V_r_c".
    struct SeamGrid {
        int n_rows = 0;
        int n_cols = 0;
        std::map<std::string, EdgeCost> edges;

        int n_edges() const { return n_rows * (n_cols - 1) + n_cols * (n_rows - 1); }
        int n_junctions() const { return (n_rows - 1) * (n_cols - 1); }
    };

    // Construit une clé d'adjacence : edge_key('H', 2, 0) == "H_2_0".
    KIRK_API std::string edge_key(char direction, int row, int col);

    // Point de jonction d'un bloc 2×2, en coordonnées normalisées dans le rectangle commun
    // des quatre overlaps : u = position verticale ∈ [0, 1], v = position horizontale ∈ [0, 1].
    struct Junction {
        double u = 0.5;
        double v = 0.5;
        double score = std::numeric_limits<double>::infinity();   // somme minimale des 4 distances
        cv::Mat score_map;    // CV_64F grid_res × grid_res : score[i][j] pour u = i/(res−1), v = j/(res−1)
        double time_s = 0.0;
    };

    // Toutes les jonctions d'une méthode, indexées par (r, c).
    struct JunctionSet {
        Method method = Method::DP;
        std::map<std::pair<int, int>, Junction> points;
        double total_time_s = 0.0;
    };

    // Distance cumulée depuis le bord "top" ou "bottom", dans le repère de `cost`.
    // Dijkstra8 → Dijkstra-8 ; DP et DPDijkstraLocal → DP. Lève std::invalid_argument si side est invalide.
    KIRK_API cv::Mat directional_dist(const cv::Mat& cost, const std::string& side, Method method);

    // Choisit J_r_c (bloc 2×2 de coin haut-gauche (r, c)) sur une grille grid_res × grid_res.
    // Lève std::invalid_argument si une des 4 adjacences du bloc manque.
    KIRK_API Junction choose_junction_point(const SeamGrid& grid, int r, int c, Method method, int grid_res = 41);

    // Choisit les (n_rows−1) × (n_cols−1) jonctions. Les cartes de distances sont libérées après
    // chaque bloc (une adjacence sert aux deux blocs voisins, mais depuis des côtés différents).
    KIRK_API JunctionSet choose_all_junctions(const SeamGrid& grid, Method method, int grid_res = 41);

    // Extrémités imposées d'une adjacence, dans le repère de SA carte de coût.
    // Absente (std::nullopt) au bord de la grille : l'extrémité reste libre.
    struct Endpoints {
        std::optional<cv::Point> start;
        std::optional<cv::Point> goal;

        // Pointeurs attendus par dp_constrained / dijkstra_path / solve_edge (nullptr = libre).
        const cv::Point* start_ptr() const { return start ? &*start : nullptr; }
        const cv::Point* goal_ptr() const { return goal ? &*goal : nullptr; }
    };

    // Convertit les J normalisés en (start, goal) pour l'adjacence `key`.
    KIRK_API Endpoints edge_endpoints(const SeamGrid& grid, const std::string& key, const JunctionSet& junctions);

    // Résout toutes les adjacences avec la méthode choisie et les jonctions imposées.
    // Renvoie un SolveResult par clé "H_r_c" / "V_r_c" (chemins dans le repère solveur).
    KIRK_API std::map<std::string, SolveResult> solve_global_network(const SeamGrid& grid, Method method,
        const JunctionSet& junctions, int corridor_radius = 18);

}  // namespace seamlib
