// include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include <opencv2/core.hpp>

#include <vector>

namespace DynamicProgrammingDijkstraNs {

    // Seamline entre deux overlaps, par carte de coût puis plus court chemin.
    //
    // Pipeline de computeSeam() :
    //
    //     overlapLeft, overlapRight  (overlaps RGB ou gris)
    //          |
    //          v
    //     computeCost()     section 5 / 5.0 : carte de coût selon le mode choisi
    //          |
    //          v
    //     solveEdge()       section 6   : chemin de coût minimal, haut -> bas,
    //          |                avec la méthode choisie (DP, Dijkstra-8, DP -> Dijkstra local)
    //          v
    //     seamline (std::vector<cv::Point>)
    //
    // Exemple :
    //
    //     CostMapParameters parameters;
    //     parameters.colorNorm = "euclidian";
    //     parameters.recall    = true;
    //
    //     SeamFinderClass finder(CostFunction::ColorGradient, parameters, SolverMethod::Dijkstra8);
    //     std::vector<cv::Point> seam = finder.computeSeam(overlapLeft, overlapRight);
    class SeamFinderClass {

        public:
            // Mode par défaut : ColorGradient, bruit, manhattan, sigmoïde, sans rappel, méthode DP.
            SeamFinderClass();

            // Mode choisi par l'utilisateur (voir cost_catalog.h et solver.h).
            SeamFinderClass(CostFunction initialCostFunction, const CostMapParameters& initialParameters = CostMapParameters(),
                            SolverMethod initialMethod = SolverMethod::DynamicProgramming);

            // Calcule la seamline verticale entre deux overlaps.
            //
            // `overlapLeft`, `overlapRight` : zone de recouvrement vue dans chaque image, même taille, 3 canaux en
            //                                 ordre RGB (cv::imread donne du BGR : convertir avant), ou 1 canal pour une
            //                                 image en gris (ex. IRR).
            //
            // Retour : chemin du haut vers le bas, en (x, y) dans le repère de l'overlap. Vide si une entrée est vide.
            //          DP : exactement un point par ligne. Dijkstra-8 et DP -> Dijkstra local : pas latéraux possibles,
            //          donc parfois plusieurs points sur une même ligne.
            //
            // Pour une paire haut/bas, transposer les overlaps (cv::transpose) puis transposer les points du résultat.
            std::vector<cv::Point> computeSeam(const cv::Mat& overlapLeft, const cv::Mat& overlapRight);

            // Réglages utilisés par computeSeam(), modifiables après construction.
            CostFunction costFunction = CostFunction::ColorGradient;
            CostMapParameters parameters;
            SolverMethod method = SolverMethod::DynamicProgramming;
            int corridorRadius = 18;   // rayon du corridor (px), utilisé seulement par DynamicProgrammingDijkstraLocal

    };

}
