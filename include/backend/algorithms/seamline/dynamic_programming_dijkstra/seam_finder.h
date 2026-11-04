
 // include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/export.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include <opencv2/core.h>

#include <vector>

namespace DynamicProgrammingDijkstraNs {

    // Classe publique de la bibliothèque permettant de calculer une seamline
    // KIRK_API permet d'exporter la classe pour une utilisation externe
    //
    // Pipeline :
    // 1. Calculer la carte de coût à partir des deux images
    // 2. Résoudre le chemin minimal avec la méthode choisie
    // 3. Renvoyer la seamline sous forme de points (x, y)
    class KIRK_API SeamFinderClass {

        public:

            // Mode par défaut : ColorGradient, bruit, Manhattan,
            // sigmoïde, sans rappel et méthode DP
            SeamFinderClass();

            // Permet de choisir la fonction de coût, les paramètres
            // et la méthode de résolution
            SeamFinderClass(
                CostFunction costFunction,
                const CostMapParams& costMapParams = CostMapParams(),
                Method method = Method::DP
            );

            // Calcule la seamline verticale entre deux zones de recouvrement
            //
            // leftImage et rightImage doivent avoir la même taille
            // Elles peuvent être RGB ou en niveaux de gris
            // cv::imread charge les images en BGR : convertir si nécessaire
            //
            // Renvoie le chemin du haut vers le bas, en coordonnées (x, y)
            // Renvoie un chemin vide si une entrée est vide
            //
            // DP : exactement un point par ligne
            // Dijkstra8 et DPDijkstraLocal : déplacements latéraux possibles
            //
            // Pour une paire haut/bas, transposer les images puis
            // les coordonnées des points du résultat
            std::vector<cv::Point> compute(
                const cv::Mat& leftImage,
                const cv::Mat& rightImage
            );

            // Réglages utilisés par compute(), modifiables après construction
            CostFunction costFunction = CostFunction::ColorGradient;
            CostMapParams params;
            Method method = Method::DP;

            // Rayon du corridor en pixels, utilisé uniquement en mode hybride
            int corridorRadius = 18;
    };

}  // namespace DynamicProgrammingDijkstraNs
