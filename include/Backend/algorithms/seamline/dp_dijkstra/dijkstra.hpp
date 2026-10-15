// =====================================================================================
// Section 6.2 · Dijkstra 8-voisins (complet ou restreint à un corridor)
// Conversion C++ des cellules « def _dijkstra_core(...) ... dijkstra_distance_map(...) » et
// « corridor_mask_from_path / hybrid_corridor » du notebook projet_kik_irpa_montauban_final.
//
// Même règle de pas que la DP (section 6.1) : (coût(p) + coût(q)) / 2 × (1 ou √2).
// Les objectifs DP, Dijkstra-8 et DP → Dijkstra local sont donc directement comparables.
//
//   Dijkstra-8          : plus court chemin EXACT, 8 voisins (pas latéraux et retours en arrière permis).
//   DP → Dijkstra local : « Dijkstra amélioré ». La DP donne un chemin initial, puis Dijkstra est
//                         restreint à un corridor (dilatation de rayon r) autour de lui : presque la
//                         qualité de Dijkstra-8 pour une fraction de son travail.
//
// Coordonnées : cv::Point = (x, y) = (colonne, ligne). Le Python stocke (y, x).
//
// Exemple :
//
//     seamlib::PathResult full  = seamlib::dijkstra_path(cost);              // Dijkstra-8, extrémités libres
//     seamlib::PathResult local = seamlib::dp_dijkstra_local(cost, 18);      // DP → Dijkstra local, r = 18
// =====================================================================================
#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "algorithms/seamline/dynamic_programming.hpp"
#include "algorithms/seamline/export.hpp"

namespace seamlib {

    // --- Dijkstra-8 ---------------------------------------------------------------------------

    // Plus court chemin 8-voisins entre le haut et le bas de `cost` (1 canal, converti en CV_64F).
    //   start   : nullptr = départ libre sur tout le bord haut.
    //   goal    : nullptr = arrivée sur le premier pixel du bord bas atteint (le moins cher).
    //   allowed : masque CV_8U de la taille de `cost` (≠ 0 = pixel autorisé). Vide = tout autorisé.
    //   keep_settled : remplit PathResult::settled (pixels explorés), utile pour illustrer.
    // Lève std::invalid_argument si start/goal sortent de la carte, std::runtime_error s'il
    // n'existe aucun chemin (ex. start hors du masque).
    KIRK_API PathResult dijkstra_path(const cv::Mat& cost, const cv::Point* start = nullptr, const cv::Point* goal = nullptr,
        const cv::Mat& allowed = cv::Mat(), bool keep_settled = false);

    // Carte des distances Dijkstra-8 depuis tout le bord haut (CV_64F), utilisée pour choisir les jonctions.
    KIRK_API cv::Mat dijkstra_distance_map(const cv::Mat& cost);

    // --- Dijkstra amélioré : DP → Dijkstra local -------------------------------------------------

    // Masque CV_8U (0 / 1) : dilatation carrée de rayon `radius` des pixels de `path`.
    KIRK_API cv::Mat corridor_mask_from_path(const std::vector<cv::Point>& path, const cv::Size& size, int radius);

    // Corridor autour du chemin DP. Les extrémités utiles restent accessibles : le point imposé,
    // ou toute la ligne du haut / du bas si l'extrémité est libre.
    KIRK_API cv::Mat hybrid_corridor(const std::vector<cv::Point>& seed_path, const cv::Size& size, int radius,
        const cv::Point* start = nullptr, const cv::Point* goal = nullptr);

    // Raccourci : dp_constrained → hybrid_corridor → dijkstra_path restreint au corridor.
    // Pour avoir en plus les temps de la graine / du raffinement et la part du corridor,
    // utiliser solve_edge(cost, Method::DPDijkstraLocal, ...) (solver.hpp).
    KIRK_API PathResult dp_dijkstra_local(const cv::Mat& cost, int radius = 18,
        const cv::Point* start = nullptr, const cv::Point* goal = nullptr);

}  // namespace seamlib
