// include/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.h
#pragma once

#include <opencv2/core.hpp>

#include <limits>
#include <string>
#include <vector>

// Section 6.3 · Résolution d'une adjacence : solveEdge
// Conversion C++ de « METHODS », « path_objective », « estimate_solver_memory_mb » et
// « solve_edge » du notebook projet_kik_irpa_montauban_final.
//
// Point d'entrée unique pour les trois méthodes :
//
//     SolverMethod::DynamicProgramming               section 6.1 : chemin monotone (↙ ↓ ↘), le plus rapide
//     SolverMethod::Dijkstra8                        section 6.2 : plus court chemin exact, 8 voisins
//     SolverMethod::DynamicProgrammingDijkstraLocal  section 6.2 : DP puis Dijkstra restreint à un corridor de rayon r
//
// Exemple :
//
//     SolveResult result = solveEdge(maps.treated, SolverMethod::DynamicProgrammingDijkstraLocal);
//     // result.path, result.objective, result.wallSeconds, result.expandedCount, result.corridorRatio, ...
namespace DynamicProgrammingDijkstraNs {

    // Les trois méthodes, dans l'ordre du tuple Python METHODS.
    enum class SolverMethod {
        DynamicProgramming,               // "DP"
        Dijkstra8,                        // "Dijkstra-8"
        DynamicProgrammingDijkstraLocal   // "DP+Dijkstra-local"
    };

    // Correspondance avec les noms du notebook. parseMethodName lève std::invalid_argument si le nom est inconnu.
    std::string getMethodName(SolverMethod method);
    SolverMethod parseMethodName(const std::string& name);
    const std::vector<SolverMethod>& getAllMethods();

    // Sortie complète de solveEdge : chemin + mesures de performance.
    struct SolveResult {
        SolverMethod method = SolverMethod::DynamicProgramming;
        std::vector<cv::Point> path;          // (x, y), du départ vers l'arrivée
        double objective = 0.0;
        double wallSeconds = 0.0;             // temps mur total
        long long expandedCount = 0;          // cellules (DP) ou nœuds (Dijkstra) traités
        long long pushCount = 0;              // insertions dans le tas (Dijkstra)
        long long maximumHeapSize = 0;        // taille max du tas (Dijkstra)
        long long cellCount = 0;              // nombre de pixels de la carte de coût
        // Champs propres à DP → Dijkstra local (NaN pour les autres méthodes) :
        double corridorRatio = std::numeric_limits<double>::quiet_NaN();   // part de la carte dans le corridor [0, 1]
        double seedSeconds = std::numeric_limits<double>::quiet_NaN();     // temps de la graine DP
        double refineSeconds = std::numeric_limits<double>::quiet_NaN();   // temps du Dijkstra local
        double estimatedMemoryMegabytes = 0.0;   // estimation analytique de la mémoire des solveurs
    };

    // Objectif d'un chemin avec la règle de pas commune (sert à vérifier / comparer les solveurs).
    // +inf si le chemin est vide.
    double computePathObjective(const cv::Mat& cost, const std::vector<cv::Point>& path);

    // Estimation (octets / pixel) des tableaux alloués par le solveur, + tas de Dijkstra (16 o / entrée).
    double estimateSolverMemoryMegabytes(SolverMethod method, const cv::Size& size, long long maximumHeapSize = 0);

    // Résout une adjacence avec la méthode choisie. `cost` : carte 1 canal (ex. CostMaps::treated).
    //   start, goal    : extrémités imposées, nullptr = libres (voir findDynamicProgrammingPath / findDijkstraPath).
    //   corridorRadius : rayon (px du solveur) du corridor, utilisé seulement par DynamicProgrammingDijkstraLocal.
    SolveResult solveEdge(const cv::Mat& cost, SolverMethod method, const cv::Point* start = nullptr,
                          const cv::Point* goal = nullptr, int corridorRadius = 18);

}
