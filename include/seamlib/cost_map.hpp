// =====================================================================================
// Section 5 · Carte de coût
// Conversion C++ de la cellule « def gradient(gray): ... compute_cost_maps(...) » du notebook
// Projet_KIK_IRPA_Montauban_final_CostMapComputed.ipynb.
//
// Convention : comme dans le notebook (lecture par tifffile), les images couleur sont en
// ordre RGB. Une image lue avec cv::imread est en BGR : la convertir avec
// cv::cvtColor(img, img, cv::COLOR_BGR2RGB) avant d'appeler ces fonctions.
// =====================================================================================
#pragma once

#include <opencv2/core.hpp>

#include <string>

#include "seamlib/export.hpp"

namespace seamlib {

    // Sortie de gradient() : équivalent du triplet Python (Gx, Gy, √(Gx² + Gy²)), tout en CV_64F.
    struct Gradients {
        cv::Mat Gx;
        cv::Mat Gy;
        cv::Mat magnitude;
    };

    // Calcul des gradients à l'aide du filtre de Sobel 3×3 : √(Gx² + Gy²).
    // `gray` : image à un canal.
    KIRK_API Gradients gradient(const cv::Mat& gray);

    // Raccourci : renvoie seulement la norme du gradient.
    KIRK_API cv::Mat gradient_magnitude(const cv::Mat& gray);

    // Normalisation min-max sur [0, 1] (tableau nul si l'image est constante).
    KIRK_API cv::Mat minmax(const cv::Mat& x);

    // MAD : médiane des écarts absolus à la médiane, sur toutes les valeurs de `img`.
    KIRK_API double mad_calculator(const cv::Mat& img);

    // Calcule le n_eme percentile de `img` (même définition que np.percentile), plafonne puis divise par lui.
    KIRK_API cv::Mat percentile(const cv::Mat& img, double n_eme);

    // Niveaux de gris en float32, sans division par dtype_max pour garder l'intégrité des unités.
    // Garde le signe si l'entrée est une différence. `img` : image RGB à 3 canaux.
    KIRK_API cv::Mat gray_raw(const cv::Mat& img);

    // Paramètres de compute_cost_maps (mêmes noms et mêmes valeurs par défaut qu'en Python).
    struct CostMapParams {
        double w_color = 0.7;
        std::string normalization = "noise";    // "noise" ou "percentile"
        std::string color_norm = "manhattan";   // "manhattan" ou "euclidian"
        std::string treatment = "sigmoid";      // "sigmoid", "linear", "squared" ou "caped"
        bool recall = false;                    // rappel vers le centre
        double lam = 0.05;                      // poids du rappel vers le centre
    };

    // Sortie de compute_cost_maps. Types identiques au Python :
    //   e_color    CV_32F
    //   e_gradient CV_64F
    //   treated    CV_32F  (seule carte utilisée par les solveurs)
    struct CostMaps {
        cv::Mat e_color;
        cv::Mat e_gradient;
        cv::Mat treated;
    };

    // Construit les cartes de coût d'une zone d'overlap.
    // `crop_a`, `crop_b` : overlaps RGB à 3 canaux, de même taille et de même type (uint8, uint16 ou float).
    // Lève std::invalid_argument si un paramètre n'est pas une valeur autorisée.
    KIRK_API CostMaps compute_cost_maps(const cv::Mat& crop_a, const cv::Mat& crop_b, const CostMapParams& params = CostMapParams());

}  // namespace seamlib
