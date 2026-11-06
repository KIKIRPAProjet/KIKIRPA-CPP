// include/backend/algorithms/seamline/dynamic_programming_dijkstra/junctions.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include <opencv2/core.hpp>

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

// Section 7 · Choix des points de jonction  +  Section 8 · Réseau global de seamlines
// Conversion C++ de « directional_dist ... choose_all_junctions » et « edge_endpoints ...
// solve_global_network » du notebook projet_kik_irpa_montauban_final.
//
// Grille de rowCount × columnCount tuiles :
//   adjacence H_r_c : tuile (r, c) | tuile (r, c+1)        r ∈ [0, rowCount),     c ∈ [0, columnCount−1)
//   adjacence V_r_c : tuile (r, c) au-dessus de (r+1, c)   r ∈ [0, rowCount−1),   c ∈ [0, columnCount)
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
//   SolverMethod::DynamicProgramming              : cartes de distances DP (computeDynamicProgrammingDistanceFromTop)
//   SolverMethod::Dijkstra8                       : cartes de distances Dijkstra-8 (computeDijkstraDistanceMap)
//   SolverMethod::DynamicProgrammingDijkstraLocal : réutilise les jonctions DP (évite 4 gros Dijkstra par jonction)
//
// Repère solveur : comme dans le notebook, une adjacence V est TRANSPOSÉE (le solveur va toujours
// du haut vers le bas). Transposer les deux overlaps haut/bas (cv::transpose) AVANT computeCost.
//
// Exemple :
//
//     SeamGrid grid;
//     grid.rowCount = 5; grid.columnCount = 3;
//     grid.edges["H_0_0"] = { costH00, 2300.0, 0.1 };   // carte, overlap (px d'origine), échelle
//     ...                                                // les 22 adjacences
//     JunctionSet junctions = chooseAllJunctions(grid, SolverMethod::DynamicProgramming);
//     std::map<std::string, SolveResult> seams = solveGlobalNetwork(grid, SolverMethod::DynamicProgramming, junctions);
namespace DynamicProgrammingDijkstraNs {

    // Une adjacence telle que la voit le solveur (équivalent des champs utiles de EdgeRecord).
    struct EdgeCost {
        cv::Mat cost;                // carte de coût 1 canal (ex. CostMaps::treated) ; adjacence V : overlaps transposés
        double overlapPixels = 0.0;  // largeur de l'overlap en pixels d'origine
        double scale = 1.0;          // pixels solveur / pixels d'origine (1 si la carte n'est pas réduite)
    };

    // Grille complète : dimensions + adjacences indexées par "H_r_c" / "V_r_c".
    struct SeamGrid {
        int rowCount = 0;
        int columnCount = 0;
        std::map<std::string, EdgeCost> edges;

        int getEdgeCount() const { return rowCount * (columnCount - 1) + columnCount * (rowCount - 1); }
        int getJunctionCount() const { return (rowCount - 1) * (columnCount - 1); }
    };

    // Construit une clé d'adjacence : buildEdgeKey('H', 2, 0) == "H_2_0".
    std::string buildEdgeKey(char direction, int row, int column);

    // Point de jonction d'un bloc 2×2, en coordonnées normalisées dans le rectangle commun
    // des quatre overlaps : normalizedRow = position verticale ∈ [0, 1] (u du notebook),
    // normalizedColumn = position horizontale ∈ [0, 1] (v du notebook).
    struct Junction {
        double normalizedRow = 0.5;
        double normalizedColumn = 0.5;
        double score = std::numeric_limits<double>::infinity();   // somme minimale des 4 distances
        cv::Mat scoreMap;           // CV_64F gridResolution × gridResolution : score[i][j] pour u = i/(res−1), v = j/(res−1)
        double timeSeconds = 0.0;
    };

    // Toutes les jonctions d'une méthode, indexées par (r, c).
    struct JunctionSet {
        SolverMethod method = SolverMethod::DynamicProgramming;
        std::map<std::pair<int, int>, Junction> points;
        double totalTimeSeconds = 0.0;
    };

    // Distance cumulée depuis le bord "top" ou "bottom", dans le repère de `cost`.
    // Dijkstra8 → Dijkstra-8 ; DynamicProgramming et DynamicProgrammingDijkstraLocal → DP. Lève std::invalid_argument
    // si side est invalide.
    cv::Mat computeDirectionalDistance(const cv::Mat& cost, const std::string& side, SolverMethod method);

    // Choisit J_r_c (bloc 2×2 de coin haut-gauche (row, column)) sur une grille gridResolution × gridResolution.
    // Lève std::invalid_argument si une des 4 adjacences du bloc manque.
    Junction chooseJunctionPoint(const SeamGrid& grid, int row, int column, SolverMethod method, int gridResolution = 41);

    // Choisit les (rowCount−1) × (columnCount−1) jonctions. Les cartes de distances sont libérées après
    // chaque bloc (une adjacence sert aux deux blocs voisins, mais depuis des côtés différents).
    JunctionSet chooseAllJunctions(const SeamGrid& grid, SolverMethod method, int gridResolution = 41);

    // Extrémités imposées d'une adjacence, dans le repère de SA carte de coût.
    // Absente (std::nullopt) au bord de la grille : l'extrémité reste libre.
    struct Endpoints {
        std::optional<cv::Point> start;
        std::optional<cv::Point> goal;

        // Pointeurs attendus par findDynamicProgrammingPath / findDijkstraPath / solveEdge (nullptr = libre).
        const cv::Point* getStartPointer() const { return start ? &*start : nullptr; }
        const cv::Point* getGoalPointer() const { return goal ? &*goal : nullptr; }
    };

    // Convertit les J normalisés en (start, goal) pour l'adjacence `key`.
    Endpoints computeEdgeEndpoints(const SeamGrid& grid, const std::string& key, const JunctionSet& junctions);

    // Résout toutes les adjacences avec la méthode choisie et les jonctions imposées.
    // Renvoie un SolveResult par clé "H_r_c" / "V_r_c" (chemins dans le repère solveur).
    std::map<std::string, SolveResult> solveGlobalNetwork(const SeamGrid& grid, SolverMethod method,
                                                          const JunctionSet& junctions, int corridorRadius = 18);

}
