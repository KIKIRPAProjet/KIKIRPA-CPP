// include/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_map.h
#pragma once

#include <opencv2/core.hpp>

#include <string>

// Section 5 · Carte de coût
// Conversion C++ de la cellule « def gradient(gray): ... compute_cost_maps(...) » du notebook
// Projet_KIK_IRPA_Montauban_final_CostMapComputed.ipynb.
//
// Convention : comme dans le notebook (lecture par tifffile), les images couleur sont en
// ordre RGB. Une image lue avec cv::imread est en BGR : la convertir avec
// cv::cvtColor(image, image, cv::COLOR_BGR2RGB) avant d'appeler ces fonctions.
namespace DynamicProgrammingDijkstraNs {

    // Sortie de computeGradients() : équivalent du triplet Python (Gx, Gy, √(Gx² + Gy²)), tout en CV_64F.
    struct Gradients {
        cv::Mat gradientX;
        cv::Mat gradientY;
        cv::Mat magnitude;
    };

    // Calcul des gradients à l'aide du filtre de Sobel 3×3 : √(Gx² + Gy²).
    // `grey` : image à un canal.
    Gradients computeGradients(const cv::Mat& grey);

    // Raccourci : renvoie seulement la norme du gradient.
    cv::Mat computeGradientMagnitude(const cv::Mat& grey);

    // Normalisation min-max sur [0, 1] (tableau nul si l'image est constante).
    cv::Mat normalizeMinMax(const cv::Mat& values);

    // MAD : médiane des écarts absolus à la médiane, sur toutes les valeurs de `image`.
    double computeMedianAbsoluteDeviation(const cv::Mat& image);

    // Calcule le percentileRank-ième percentile de `image` (même définition que np.percentile), plafonne puis divise
    // par lui.
    cv::Mat normalizeByPercentile(const cv::Mat& image, double percentileRank);

    // Niveaux de gris en float32, sans division par la valeur maximale du type pour garder l'intégrité des unités.
    // Garde le signe si l'entrée est une différence. `image` : image RGB à 3 canaux, ou déjà en gris
    // (1 canal, renvoyée telle quelle en float32).
    cv::Mat convertToRawGrey(const cv::Mat& image);

    // Paramètres de computeCostMaps (mêmes valeurs par défaut qu'en Python ; nom Python entre parenthèses).
    struct CostMapParameters {
        double colorWeight = 0.7;               // (w_color)
        std::string normalization = "noise";    // "noise" ou "percentile"
        std::string colorNorm = "manhattan";    // (color_norm) "manhattan" ou "euclidian"
        std::string treatment = "sigmoid";      // "sigmoid", "linear", "squared" ou "caped"
        bool recall = false;                    // rappel vers le centre
        double recallWeight = 0.05;             // (lam) poids du rappel vers le centre
    };

    // Sortie de computeCostMaps. Types identiques au Python :
    //   colorTerm    CV_32F   (e_color)
    //   gradientTerm CV_64F   (e_gradient)
    //   treated      CV_32F   (seule carte utilisée par les solveurs)
    struct CostMaps {
        cv::Mat colorTerm;
        cv::Mat gradientTerm;
        cv::Mat treated;
    };

    // Construit les cartes de coût d'une zone d'overlap.
    // `overlapA`, `overlapB` : overlaps RGB à 3 canaux ou en gris à 1 canal (ex. IRR), de même taille, même
    // nombre de canaux et même type (uint8, uint16 ou float). En gris, le terme couleur vaut |A − B|.
    // Lève std::invalid_argument si un paramètre n'est pas une valeur autorisée.
    CostMaps computeCostMaps(const cv::Mat& overlapA, const cv::Mat& overlapB,
                             const CostMapParameters& parameters = CostMapParameters());

}
