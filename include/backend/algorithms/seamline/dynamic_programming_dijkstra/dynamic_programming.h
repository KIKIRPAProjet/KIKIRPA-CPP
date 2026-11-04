// include/backend/algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h
#pragma once

#include <opencv2/core.hpp>

#include <vector>

// Section 6.1 · Programmation dynamique
// Conversion C++ de la cellule « def _dp_step_numpy(...) ... dp_distance_top(...) » du notebook
// Projet_KIK_IRPA_Montauban_final_CostMapComputed.ipynb.
//
// Coût d'un pas entre deux pixels voisins p et q : (coût(p) + coût(q)) / 2 × longueur du pas (1 ou √2).
// Objectif d'un chemin = coût(départ) + somme des coûts de pas.
//
// Coordonnées : les chemins sont des cv::Point. Attention, cv::Point est (x, y) = (colonne, ligne),
// alors que le Python stocke (y, x).
namespace DynamicProgrammingDijkstraNs {

    // Éléments de la section 6 (outils communs) dont la 6.1 a besoin
    const double SQRT2 = 1.4142135623730951;

    // Poids d'un pas entre (row, column) et (neighbourRow, neighbourColumn). `cost` doit être CV_64F.
    double computeEdgeWeight(const cv::Mat& cost, int row, int column, int neighbourRow, int neighbourColumn);

    // Sortie brute d'un solveur élémentaire (programmation dynamique, Dijkstra-8 ou DP → Dijkstra local).
    struct PathResult {
        std::vector<cv::Point> path;    // du départ vers l'arrivée (DP : une ligne par point)
        double objective = 0.0;
        long long expandedCount = 0;    // cellules traitées (DP) ou nœuds fixés (Dijkstra)
        long long pushCount = 0;        // insertions dans le tas (Dijkstra uniquement)
        long long maximumHeapSize = 0;  // taille max du tas (Dijkstra uniquement)
        cv::Mat settledMask;            // CV_8U, 1 = pixel exploré (Dijkstra avec keepSettled, sinon vide)
    };

    // Section 6.1

    // DP monotone (le chemin descend d'une ligne à chaque pas, décalage latéral ≤ 1) avec
    // extrémités éventuellement imposées. `start` ou `goal` à nullptr = extrémité libre
    // (équivalent de None en Python) : départ sur toute la première ligne, arrivée au minimum
    // de la dernière ligne. Lève std::invalid_argument si start.y > goal.y, std::runtime_error
    // s'il n'existe aucun chemin.
    PathResult findDynamicProgrammingPath(const cv::Mat& cost, const cv::Point* start = nullptr,
                                          const cv::Point* goal = nullptr);

    // Carte des distances cumulées depuis le bord haut (CV_64F), utilisée pour choisir les jonctions.
    cv::Mat computeDynamicProgrammingDistanceFromTop(const cv::Mat& cost);

}
