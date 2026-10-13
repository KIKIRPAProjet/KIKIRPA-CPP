#include "seamlib/cost_catalog.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace seamlib {

    namespace {

        // --- Outils de la section 3 nécessaires au catalogue -----------------------------------

        // Valeur maximale du type (np.iinfo(dtype).max), 1 pour les flottants.
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
        cv::Mat to_gray(const cv::Mat& img) {
            cv::Mat f;
            img.convertTo(f, CV_MAKETYPE(CV_32F, img.channels()), 1.0 / dtype_max(img));
            f = cv::max(f, 0.0);
            f = cv::min(f, 1.0);
            if (f.channels() == 1) return f;
            cv::Mat g;
            cv::cvtColor(f, g, cv::COLOR_RGB2GRAY);
            return g;
        }

        // Équivalent de np.percentile (interpolation linéaire) : renvoie la valeur.
        double percentile_value(const cv::Mat& m, double q) {
            cv::Mat d;
            m.reshape(1, 1).convertTo(d, CV_64F);
            std::vector<double> v(d.begin<double>(), d.end<double>());
            const double idx = q / 100.0 * double(v.size() - 1);
            const size_t lo = size_t(std::floor(idx));
            const size_t hi = std::min(lo + 1, v.size() - 1);
            std::nth_element(v.begin(), v.begin() + lo, v.end());
            const double a = v[lo];
            const double b = (hi == lo) ? a : *std::min_element(v.begin() + lo + 1, v.end());
            return a + (b - a) * (idx - double(lo));
        }

        // Ajoute une pénalité au coût traité, en restant en CV_32F comme le Python.
        CostMaps add_to_treated(CostMaps m, const cv::Mat& penalty) {
            cv::Mat p;
            penalty.convertTo(p, CV_32F);
            m.treated = m.treated + p;
            return m;
        }

        cv::Mat local_normalize(const cv::Mat& g, int win) {
            cv::Mat mu, mu2;
            cv::blur(g, mu, cv::Size(win, win));
            cv::blur(g.mul(g), mu2, cv::Size(win, win));
            cv::Mat var = mu2 - mu.mul(mu);
            var = cv::max(var, 1e-4);
            cv::Mat sd;
            cv::sqrt(var, sd);
            return (g - mu) / sd;
        }

    }  // namespace

    // --- Fonctions du catalogue ---------------------------------------------------------------

    CostMaps cost_color_gradient(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        return compute_cost_maps(a, b, params);
    }

    CostMaps cost_color_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        CostMapParams p = params;
        p.w_color = 1.0;
        return compute_cost_maps(a, b, p);
    }

    CostMaps cost_gradient_only(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        CostMapParams p = params;
        p.w_color = 0.0;
        return compute_cost_maps(a, b, p);
    }

    CostMaps cost_grain_robust(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double sigma) {
        // Flou en unités brutes : le grain du capteur ne guide plus le chemin.
        cv::Mat fa, fb;
        a.convertTo(fa, CV_MAKETYPE(CV_32F, a.channels()));
        b.convertTo(fb, CV_MAKETYPE(CV_32F, b.channels()));
        cv::GaussianBlur(fa, fa, cv::Size(0, 0), sigma);
        cv::GaussianBlur(fb, fb, cv::Size(0, 0), sigma);
        return compute_cost_maps(fa, fb, params);
    }

    CostMaps cost_local_norm(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params) {
        // Égalisation locale (moyenne et écart-type par fenêtre) : insensible aux variations d'éclairage.
        const cv::Mat ga = to_gray(a), gb = to_gray(b);
        const int win = std::max(5, (std::min(ga.rows, ga.cols) / 8) | 1);
        const cv::Mat na = local_normalize(ga, win), nb = local_normalize(gb, win);

        cv::Mat e_color = cv::abs(na - nb);
        e_color = cv::min(e_color, percentile_value(e_color, 99));    // écrête les valeurs aberrantes
        cv::Mat e_grad = 0.5 * (gradient_magnitude(na) + gradient_magnitude(nb));
        e_grad = cv::min(e_grad, percentile_value(e_grad, 99));
        e_color = minmax(e_color);
        e_grad = minmax(e_grad);

        cv::Mat wc;
        cv::Mat(e_color * params.w_color).convertTo(wc, CV_64F);
        const cv::Mat combined = wc + (1.0 - params.w_color) * e_grad;

        CostMaps out;
        out.e_color = e_color;                       // CV_32F
        e_grad.convertTo(out.e_gradient, CV_32F);
        combined.convertTo(out.treated, CV_32F);
        return out;
    }

    CostMaps cost_highlight_penalty(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double threshold,
        double weight) {
        // Pénalité sur les pixels proches de la saturation dans A ou B : la seamline contourne les reflets de vernis.
        cv::Mat brightest;
        cv::max(to_gray(a), to_gray(b), brightest);
        cv::Mat penalty = (brightest - threshold) / (1.0 - threshold);
        penalty = cv::max(penalty, 0.0);
        penalty = cv::min(penalty, 1.0);
        return add_to_treated(compute_cost_maps(a, b, params), weight * penalty);
    }

    CostMaps cost_center_prior(const cv::Mat& a, const cv::Mat& b, const CostMapParams& params, double weight) {
        // Pénalité quadratique de l'écart au centre : sur un fond uni, la seamline reste droite et centrée.
        const int w = a.cols;
        cv::Mat prior(1, w, CV_32F);
        for (int x = 0; x < w; ++x) {
            const float xv = (w == 1) ? -1.0f : float(-1.0 + 2.0 * x / (w - 1));   // np.linspace(-1, 1, w)
            prior.at<float>(0, x) = xv * xv;
        }
        return add_to_treated(compute_cost_maps(a, b, params), weight * cv::repeat(prior, a.rows, 1));
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

    const std::vector<CostFunction>& all_cost_functions() {
        static const std::vector<CostFunction> all = {
            CostFunction::ColorGradient, CostFunction::ColorOnly,        CostFunction::GradientOnly,
            CostFunction::GrainRobust,   CostFunction::LocalNorm,        CostFunction::HighlightPenalty,
            CostFunction::CenterPrior };
        return all;
    }

    std::string cost_function_name(CostFunction fn) {
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

    CostFunction cost_function_from_name(const std::string& name) {
        for (CostFunction fn : all_cost_functions())
            if (cost_function_name(fn) == name) return fn;
        throw std::invalid_argument("Fonction de coût inconnue : " + name);
    }

}  // namespace seamlib
