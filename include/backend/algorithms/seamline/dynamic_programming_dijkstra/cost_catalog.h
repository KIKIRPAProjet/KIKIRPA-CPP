// include/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// Section 5.0 · Catalogue des fonctions de coût
// Conversion C++ de la cellule « def _baseline(...) ... COST_FUNCTIONS = {...} » du notebook
// Projet_KIK_IRPA_Montauban_final_CostMapComputed.ipynb.
//
// Chaque fonction reçoit deux overlaps de même taille (RGB à 3 canaux ou gris à 1 canal) et renvoie les trois cartes
// (terme couleur, terme gradient, coût traité). Seul `treated` est utilisé par les solveurs ;
// les deux premiers termes servent à l'illustration.
//
// ⚠️ Ces cartes ne servent qu'à CHOISIR le chemin. L'image finale reste un « hard-cut » de pixels
//    réels : aucun flou ni aucune égalisation n'est appliqué aux pixels de la mosaïque.
//
// Exemple : fonction de référence, bruit, norme euclidienne, sigmoïde, rappel vers le centre.
//
//     DynamicProgrammingDijkstraNs::CostMapParameters parameters;
//     parameters.normalization = "noise";
//     parameters.colorNorm     = "euclidian";
//     parameters.treatment     = "sigmoid";
//     parameters.recall        = true;
//     DynamicProgrammingDijkstraNs::CostMaps maps = DynamicProgrammingDijkstraNs::computeCost(
//         overlapA, overlapB, DynamicProgrammingDijkstraNs::CostFunction::ColorGradient, parameters);
//     // maps.treated se passe ensuite à findDynamicProgrammingPath(...)
namespace DynamicProgrammingDijkstraNs {

    // Les sept fonctions du catalogue, dans l'ordre du dict Python COST_FUNCTIONS.
    enum class CostFunction {
        ColorGradient,        // "color_gradient"    : référence, w·couleur + (1−w)·gradient, réglée par CostMapParameters
        ColorOnly,            // "color_only"        : référence avec colorWeight = 1 (couleur seule)
        GradientOnly,         // "gradient_only"     : référence avec colorWeight = 0 (écart de gradient seul)
        GrainRobust,          // "grain_robust"      : référence sur des overlaps légèrement floutés (σ = 1,5 px)
        LocalNormalization,   // "local_norm"        : différence après égalisation locale (voir note ci-dessous)
        HighlightPenalty,     // "highlight_penalty" : référence + pénalité sur les pixels proches de la saturation
        CenterPrior           // "center_prior"      : référence + pénalité quadratique d'écart au centre
    };

    // Point d'entrée unique : calcule la carte de coût avec la fonction et les réglages choisis.
    // Les réglages (normalization, colorNorm, treatment, recall, recallWeight, colorWeight) sont transmis à
    // computeCostMaps ; une fonction peut en fixer certains (ex. ColorOnly impose colorWeight = 1).
    CostMaps computeCost(const cv::Mat& overlapA, const cv::Mat& overlapB, CostFunction costFunction,
                         const CostMapParameters& parameters = CostMapParameters());

    // Correspondance avec les noms du notebook (utile pour lire une configuration texte).
    // parseCostFunctionName lève std::invalid_argument si le nom est inconnu.
    CostFunction parseCostFunctionName(const std::string& name);
    std::string getCostFunctionName(CostFunction costFunction);
    const std::vector<CostFunction>& getAllCostFunctions();

    // Fonctions du catalogue, utilisables directement
    CostMaps computeColorGradientCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters);
    CostMaps computeColorOnlyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters);
    CostMaps computeGradientOnlyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters);
    CostMaps computeGrainRobustCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                    double blurSigma = 1.5);
    // Note : comme en Python, local_norm n'utilise que colorWeight. Elle ignore normalization, colorNorm,
    // treatment et recall, et ses cartes sont ramenées sur [0, 1] par min-max.
    CostMaps computeLocalNormalizationCost(const cv::Mat& overlapA, const cv::Mat& overlapB,
                                           const CostMapParameters& parameters);
    CostMaps computeHighlightPenaltyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                         double threshold = 0.92, double weight = 1.0);
    CostMaps computeCenterPriorCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                    double weight = 0.15);

}
