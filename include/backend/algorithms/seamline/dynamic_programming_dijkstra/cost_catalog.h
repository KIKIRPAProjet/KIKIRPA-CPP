// =====================================================================================
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
//     seamlib::CostMapParams p;
//     p.normalization = "noise";
//     p.color_norm    = "euclidian";
//     p.treatment     = "sigmoid";
//     p.recall        = true;
//     seamlib::CostMaps m = seamlib::compute_cost(a, b, seamlib::CostFunction::ColorGradient, p);
//     // m.treated se passe ensuite à seamlib::dp_constrained(...)
// =====================================================================================
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.hpp"
#include "algorithms/seamline/dynamic_programming_dijkstra/export.hpp"

#include <string>
#include <vector>

namespace DynamicProgrammingDijkstraNS {

    // Les sept fonctions du catalogue, dans l'ordre du dict Python COST_FUNCTIONS.
    enum class CostFunction {
        ColorGradient,      // "color_gradient"    : référence, w·couleur + (1−w)·gradient, réglée par CostMapParams
        ColorOnly,          // "color_only"        : référence avec w_color = 1 (couleur seule)
        GradientOnly,       // "gradient_only"     : référence avec w_color = 0 (écart de gradient seul)
        GrainRobust,        // "grain_robust"      : référence sur des overlaps légèrement floutés (σ = 1,5 px)
        LocalNorm,          // "local_norm"        : différence après égalisation locale (voir note ci-dessous)
        HighlightPenalty,   // "highlight_penalty" : référence + pénalité sur les pixels proches de la saturation
        CenterPrior         // "center_prior"      : référence + pénalité quadratique d'écart au centre
    };

    // Point d'entrée unique : calcule la carte de coût avec la fonction et les réglages choisis.
    // Les réglages (normalization, color_norm, treatment, recall, lam, w_color) sont transmis à
    // compute_cost_maps ; une fonction peut en fixer certains (ex. ColorOnly impose w_color = 1).
    KIRK_API CostMaps compute_cost(const cv::Mat& a, const cv::Mat& b, CostFunction fn, const CostMapParams& params = CostMapParams());

    // Correspondance avec les noms du notebook (utile pour lire une configuration texte).
    // Lève std::invalid_argument si le nom est inconnu.
    KIRK_API CostFunction cost_function_from_name(const std::string& name);
    KIRK_API std::string cost_function_name(CostFunction fn);
    KIRK_API const std::vector<CostFunction>& all_cost_functions();

    // --- Fonctions du catalogue, utilisables directement ------------------------------------
    KIRK_API CostMaps cost_color_gradient(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params);
    KIRK_API CostMaps cost_color_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params);
    KIRK_API CostMaps cost_gradient_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params);
    KIRK_API CostMaps cost_grain_robust(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double sigma = 1.5);
    // Note : comme en Python, local_norm n'utilise que w_color. Elle ignore normalization, color_norm,
    // treatment et recall, et ses cartes sont ramenées sur [0, 1] par min-max.
    KIRK_API CostMaps cost_local_norm(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params);
    KIRK_API CostMaps cost_highlight_penalty(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params,
        double threshold = 0.92, double weight = 1.0);
    KIRK_API CostMaps cost_center_prior(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double weight = 0.15);

}  // namespace seamlib
