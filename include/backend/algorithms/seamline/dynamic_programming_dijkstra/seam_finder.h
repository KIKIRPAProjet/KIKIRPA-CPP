#pragma once

#include <opencv2/core.h>
#include <vector>

#include "algorithms/seamline/dynamic_programming_dijkstra/export.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"


namespace DynamicProgrammingDijkstraNS
{

    /*
        Classe publique de la bibliothèque.

        KIRK_API permet à cette classe d'être utilisable :

        - depuis une DLL KIRKSeamlines sous Windows ;
        - directement depuis une librairie statique ;
        - depuis une application cliente externe.

        Pipeline de compute() :

            left, right  (overlaps RGB ou gris)
                 |
                 v
            compute_cost()     section 5 / 5.0 : carte de coût selon le mode choisi
                 |
                 v
            solve_edge()       section 6   : chemin de coût minimal, haut -> bas,
                 |                 avec la méthode choisie (DP, Dijkstra-8, DP -> Dijkstra local)
                 |
                 v
            seamline (std::vector<cv::Point>)

        Exemple :

            seamlib::CostMapParams p;
            p.color_norm = "euclidian";
            p.recall     = true;

            seamlib::SeamFinder finder(seamlib::CostFunction::ColorGradient, p, seamlib::Method::Dijkstra8);
            std::vector<cv::Point> seam = finder.compute(left, right);
    */
    class KIRK_API SeamFinder
    {

    public:

        // Mode par défaut : ColorGradient, bruit, manhattan, sigmoïde, sans rappel, méthode DP.
        SeamFinder();

        // Mode choisi par l'utilisateur (voir cost_catalog.hpp et solver.hpp).
        SeamFinder(CostFunction cost_function, const CostMapParams& params = CostMapParams(), Method method = Method::DP);


        /*
            Calcule la seamline verticale entre deux overlaps.

            `left`, `right` : zone de recouvrement vue dans chaque image,
                              même taille, 3 canaux en ordre RGB
                              (cv::imread donne du BGR : convertir avant),
                              ou 1 canal pour une image en gris (ex. IRR).

            Retour : chemin du haut vers le bas, en (x, y) dans le repère de
                     l'overlap. Vide si une entrée est vide.
                     DP : exactement un point par ligne. Dijkstra-8 et
                     DP -> Dijkstra local : pas latéraux possibles, donc
                     parfois plusieurs points sur une même ligne.

            Pour une paire haut/bas, transposer les overlaps (cv::transpose)
            puis transposer les points du résultat.
        */
        std::vector<cv::Point> compute(
            const cv::Mat& left,
            const cv::Mat& right
        );


        // Réglages utilisés par compute(), modifiables après construction.
        CostFunction cost_function = CostFunction::ColorGradient;
        CostMapParams params;
        Method method = Method::DP;
        int corridor_radius = 18;    // rayon du corridor (px), utilisé seulement par Method::DPDijkstraLocal

    };

}
