// =====================================================================================
// Section 6.3 · Résolution d'une adjacence : solve_edge
// Conversion C++ de « METHODS », « path_objective », « estimate_solver_memory_mb » et
// « solve_edge » du notebook projet_kik_irpa_montauban_final.
//
// Point d'entrée unique pour les trois méthodes :
//
//     Method::DP               section 6.1 : chemin monotone (↙ ↓ ↘), le plus rapide
//     Method::Dijkstra8        section 6.2 : plus court chemin exact, 8 voisins
//     Method::DPDijkstraLocal  section 6.2 : DP puis Dijkstra restreint à un corridor de rayon r
//
// Exemple :
//
//     seamlib::SolveResult r = seamlib::solve_edge(m.treated, seamlib::Method::DPDijkstraLocal);
//     // r.path, r.objective, r.wall_s, r.expanded, r.corridor_ratio, ...
// =====================================================================================
#pragma once

#include <opencv2/core.hpp>

#include <limits>
#include <string>
#include <vector>

#include "algorithms/seamline/export.hpp"

namespace seamlib {

    // Les trois méthodes, dans l'ordre du tuple Python METHODS.
    enum class Method {
        DP,                 // "DP"
        Dijkstra8,          // "Dijkstra-8"
        DPDijkstraLocal     // "DP+Dijkstra-local"
    };

    // Correspondance avec les noms du notebook. method_from_name lève std::invalid_argument si inconnu.
    KIRK_API std::string method_name(Method m);
    KIRK_API Method method_from_name(const std::string& name);
    KIRK_API const std::vector<Method>& all_methods();

    // Sortie complète de solve_edge : chemin + mesures de performance.
    struct SolveResult {
        Method method = Method::DP;
        std::vector<cv::Point> path;          // (x, y), du départ vers l'arrivée
        double objective = 0.0;
        double wall_s = 0.0;                  // temps mur total
        long long expanded = 0;               // cellules (DP) ou nœuds (Dijkstra) traités
        long long pushes = 0;                 // insertions dans le tas (Dijkstra)
        long long max_heap = 0;               // taille max du tas (Dijkstra)
        long long cells = 0;                  // nombre de pixels de la carte de coût
        // Champs propres à DP → Dijkstra local (NaN pour les autres méthodes) :
        double corridor_ratio = std::numeric_limits<double>::quiet_NaN();   // part de la carte dans le corridor [0, 1]
        double seed_s = std::numeric_limits<double>::quiet_NaN();           // temps de la graine DP
        double refine_s = std::numeric_limits<double>::quiet_NaN();         // temps du Dijkstra local
        double mem_est_mb = 0.0;              // estimation analytique de la mémoire des solveurs
    };

    // Objectif d'un chemin avec la règle de pas commune (sert à vérifier / comparer les solveurs).
    // +inf si le chemin est vide.
    KIRK_API double path_objective(const cv::Mat& cost, const std::vector<cv::Point>& path);

    // Estimation (octets / pixel) des tableaux alloués par le solveur, + tas de Dijkstra (16 o / entrée).
    KIRK_API double estimate_solver_memory_mb(Method method, const cv::Size& size, long long max_heap = 0);

    // Résout une adjacence avec la méthode choisie. `cost` : carte 1 canal (ex. CostMaps::treated).
    //   start, goal     : extrémités imposées, nullptr = libres (voir dp_constrained / dijkstra_path).
    //   corridor_radius : rayon (px du solveur) du corridor, utilisé seulement par DPDijkstraLocal.
    KIRK_API SolveResult solve_edge(const cv::Mat& cost, Method method, const cv::Point* start = nullptr,
        const cv::Point* goal = nullptr, int corridor_radius = 18);

}  // namespace seamlib
