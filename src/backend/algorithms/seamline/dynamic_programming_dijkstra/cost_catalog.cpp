#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"

#include <opencv2/imgproc.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace DynamicProgrammingDijkstraNS {

    namespace {

        // --- Outils de la section 3 nécessaires au catalogue -----------------------------------

        // Valeur maximale positive du type (np.iinfo(dtype).max), 1 pour les flottants.
        double dtype_max(const cv::Mat& img) {
            switch (img.depth()) {
            case CV_8U: return 255.0;
            case CV_8S: return 127.0;
            case CV_16U: return 65535.0;
            case CV_16S: return 32767.0;
            case CV_32S: return 2147483647.0;
            default: return 1.0;
            }
        }

        // to_float01 puis passage en gris : CV_32F, valeurs dans [0, 1].
        cv::Mat to_gray(const cv::Mat& img) { //converts image into floating-point grayscale values 0=<x=<1
            cv::Mat f;
            img.convertTo(f, CV_MAKETYPE(CV_32F, img.channels()), 1.0 / dtype_max(img));
            f = cv::max(f, 0.0);
            f = cv::min(f, 1.0);
            if (f.channels() == 1) return f;
            cv::Mat g;
            cv::cvtColor(f, g, cv::COLOR_RGB2GRAY); //converts from RGB to grayscale
            return g;
        }

        // Équivalent de np.percentile (interpolation linéaire) : renvoie la valeur.
        double percentile_value(const cv::Mat& m, double q) {
            cv::Mat d;
            m.reshape(1, 1).convertTo(d, CV_64F); //flatten the matrix into a list + converts its value to double
            std::vector<double> v(d.begin<double>(), d.end<double>());
            const double idx = q / 100.0 * double(v.size() - 1); //calculate the percentile's position
            const size_t lo = size_t(std::floor(idx));
            const size_t hi = std::min(lo + 1, v.size() - 1);
            std::nth_element(v.begin(), v.begin() + lo, v.end()); //finds surrounding values
            const double a = v[lo];
            const double b = (hi == lo) ? a : *std::min_element(v.begin() + lo + 1, v.end()); //finds surrounding values
            return a + (b - a) * (idx - double(lo)); //interpolates if necessary
        }

        // Ajoute une pénalité au coût traité, en restant en CV_32F comme le Python.
        CostMaps add_to_treated(CostMaps m, const cv::Mat& penalty) {
            cv::Mat p;
            penalty.convertTo(p, CV_32F);
            m.treated = m.treated + p;
            return m;
        }

        // express each pixel relative to the brightness and contrast of its neighborhood
        cv::Mat local_normalize(const cv::Mat& g, int win) {
            cv::Mat mu, mu2;
            cv::blur(g, mu, cv::Size(win, win)); //computes the local average
            cv::blur(g.mul(g), mu2, cv::Size(win, win)); //computes the local mean square value
            cv::Mat var = mu2 - mu.mul(mu); //computes the local variance
            var = cv::max(var, 1e-4); //prevents the variance from becoming too small
            cv::Mat sd;
            cv::sqrt(var, sd);
            return (g - mu) / sd; //returns the locally normalized image
        }

    }  // namespace

    // --- Fonctions du catalogue ---------------------------------------------------------------
    // a = first image
    // b = corresponding region from the second image
    // params = settings controlling the cost calculation
    CostMaps cost_color_gradient(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        return compute_cost_maps(a, b, params); //defined elsewhere
    } // uses the standard cost calculation with supplied settings

    CostMaps cost_color_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        CostMapParams p = params; //copies the parameters
        p.w_color = 1.0; //set color weight to 1
        return compute_cost_maps(a, b, p);
    }

    CostMaps cost_gradient_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        CostMapParams p = params;
        p.w_color = 0.0; //set color weight to 0
        return compute_cost_maps(a, b, p); //call the standard calculator
    }

    //Blurs both images before calculating the standard cost -> stop small variations (sensor noise, grain) from controlling the seam
    CostMaps cost_grain_robust(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double sigma) {
        // Flou en unités brutes : le grain du capteur ne guide plus le chemin.
        // sigma controls the amount of blur
        cv::Mat fa, fb;
        a.convertTo(fa, CV_MAKETYPE(CV_32F, a.channels())); //converts to floating-point
        b.convertTo(fb, CV_MAKETYPE(CV_32F, b.channels())); //converts to floating-point
        cv::GaussianBlur(fa, fa, cv::Size(0, 0), sigma); //smooth
        cv::GaussianBlur(fb, fb, cv::Size(0, 0), sigma); //smooth
        return compute_cost_maps(fa, fb, params);
    }

    CostMaps cost_local_norm(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        // Égalisation locale (moyenne et écart-type par fenêtre) : insensible aux variations d'éclairage.
        const cv::Mat ga = to_gray(a), gb = to_gray(b); //converts to grayscale
        const int win = std::max(5, (std::min(ga.rows, ga.cols) / 8) | 1); //choose a neighborhood size
        const cv::Mat na = local_normalize(ga, win), nb = local_normalize(gb, win); //normalize both images

        cv::Mat e_color = cv::abs(na - nb); //calculate difference between normalized grayscale images
        e_color = cv::min(e_color, percentile_value(e_color, 99));    // écrête les valeurs aberrantes
        cv::Mat e_grad = 0.5 * (gradient_magnitude(na) + gradient_magnitude(nb)); // measures average gradient strength
        e_grad = cv::min(e_grad, percentile_value(e_grad, 99));
        e_color = minmax(e_color); //limit value
        e_grad = minmax(e_grad); //limit value

        cv::Mat wc;
        cv::Mat(e_color * params.w_color).convertTo(wc, CV_64F);
        const cv::Mat combined = wc + (1.0 - params.w_color) * e_grad;

        CostMaps out;
        out.e_color = e_color;                       // CV_32F, map mesuring appearance differences
        e_grad.convertTo(out.e_gradient, CV_32F);    // map related to changes in image intensity
        combined.convertTo(out.treated, CV_32F);    // final cost map used for choosing the seam
        return out;
    }

    // adds penalty in bright area to discourage the seam from crossing highlights
    CostMaps cost_highlight_penalty(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double threshold,
        double weight) {
        // Pénalité sur les pixels proches de la saturation dans A ou B : la seamline contourne les reflets de vernis.
        cv::Mat brightest;
        cv::max(to_gray(a), to_gray(b), brightest); //at each pixels : keeps the brighter grayscale value from either image
        cv::Mat penalty = (brightest - threshold) / (1.0 - threshold); //penalty clipped to [0,1]
        penalty = cv::max(penalty, 0.0);
        penalty = cv::min(penalty, 1.0);
        return add_to_treated(compute_cost_maps(a, b, params), weight * penalty);
    }

    // adds penalty that grows toward the left and right edges
    CostMaps cost_center_prior(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double weight) {
        // Pénalité quadratique de l'écart au centre : sur un fond uni, la seamline reste droite et centrée.
        const int w = a.cols;
        cv::Mat prior(1, w, CV_32F);
        for (int x = 0; x < w; ++x) {
            // for every column : calculates a position between -1 and 1
            const float xv = (w == 1) ? -1.0f : float(-1.0 + 2.0 * x / (w - 1));   // np.linspace(-1, 1, w)
            prior.at<float>(0, x) = xv * xv; //squares that position
        }
        return add_to_treated(compute_cost_maps(a, b, params), weight * cv::repeat(prior, a.rows, 1)); // panalty row is repeated over the image height
    }

    // --- Sélection du mode ----------------------------------------------------------------------

    CostMaps compute_cost(const cv::Mat& a, const cv::Mat& b, CostFunction fn, const CostMapParams& params) {
        switch (fn) {
        case CostFunction::ColorGradient: return cost_color_gradient(a, b, params);
        case CostFunction::ColorOnly: return cost_color_only(a, b, params);
        case CostFunction::GradientOnly: return cost_gradient_only(a, b, params);
        case CostFunction::GrainRobust: return cost_grain_robust(a, b, params);
        case CostFunction::LocalNorm: return cost_local_norm(a, b, params);
        case CostFunction::HighlightPenalty: return cost_highlight_penalty(a, b, params);
        case CostFunction::CenterPrior: return cost_center_prior(a, b, params);
        }
        throw std::invalid_argument("Fonction de coût inconnue"); 
    }

    const std::vector<CostFunction>& all_cost_functions() { //returns a list containing all 7 modes available
        static const std::vector<CostFunction> all = {
            CostFunction::ColorGradient, CostFunction::ColorOnly,        CostFunction::GradientOnly,
            CostFunction::GrainRobust,   CostFunction::LocalNorm,        CostFunction::HighlightPenalty,
            CostFunction::CenterPrior };
        return all;
    }

    std::string cost_function_name(CostFunction fn) { //converts a mode into its text name
        switch (fn) {
        case CostFunction::ColorGradient: return "color_gradient";
        case CostFunction::ColorOnly: return "color_only";
        case CostFunction::GradientOnly: return "gradient_only";
        case CostFunction::GrainRobust: return "grain_robust";
        case CostFunction::LocalNorm: return "local_norm";
        case CostFunction::HighlightPenalty: return "highlight_penalty";
        case CostFunction::CenterPrior: return "center_prior";
        }
        throw std::invalid_argument("Fonction de coût inconnue");
    }

    CostFunction cost_function_from_name(const std::string& name) { //performs the reverse conversion
        for (CostFunction fn : all_cost_functions())
            if (cost_function_name(fn) == name) return fn;
        throw std::invalid_argument("Fonction de coût inconnue : " + name);
    }

}  // namespace seamlib
