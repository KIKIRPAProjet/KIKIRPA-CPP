#include "seamlib/cost_map.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace seamlib {

    namespace {

        // Copie toutes les valeurs (tous canaux) dans un vecteur de doubles.
        std::vector<double> to_vector(const cv::Mat& m) {
            cv::Mat d;
            m.reshape(1, 1).convertTo(d, CV_64F);
            return std::vector<double>(d.begin<double>(), d.end<double>());
        }

        // Équivalent de np.median.
        double median(std::vector<double> v) {
            if (v.empty()) throw std::invalid_argument("median : tableau vide");
            const size_t mid = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + mid, v.end());
            const double hi = v[mid];
            if (v.size() % 2 == 1) return hi;
            const double lo = *std::max_element(v.begin(), v.begin() + mid);
            return 0.5 * (lo + hi);
        }

        // Équivalent de np.percentile (méthode par défaut : interpolation linéaire).
        double np_percentile(std::vector<double> v, double q) {
            if (v.empty()) throw std::invalid_argument("percentile : tableau vide");
            const double idx = q / 100.0 * double(v.size() - 1);
            const size_t lo = size_t(std::floor(idx));
            const size_t hi = std::min(lo + 1, v.size() - 1);
            std::nth_element(v.begin(), v.begin() + lo, v.end());
            const double a = v[lo];
            const double b = (hi == lo) ? a : *std::min_element(v.begin() + lo + 1, v.end());
            return a + (b - a) * (idx - double(lo));
        }

        bool is_one_of(const std::string& s, std::initializer_list<const char*> allowed) {
            for (const char* a : allowed)
                if (s == a) return true;
            return false;
        }

    }  // namespace

    Gradients gradient(const cv::Mat& gray) {
        CV_Assert(gray.channels() == 1);
        Gradients g;
        cv::Sobel(gray, g.Gx, CV_64F, 1, 0, 3);
        cv::Sobel(gray, g.Gy, CV_64F, 0, 1, 3);
        cv::magnitude(g.Gx, g.Gy, g.magnitude);   // calcule sqrt(Gx^2 + Gy^2) élément par élément
        return g;
    }

    cv::Mat gradient_magnitude(const cv::Mat& gray) {
        return gradient(gray).magnitude;
    }

    cv::Mat minmax(const cv::Mat& x) {
        double lo = 0.0, hi = 0.0;
        cv::minMaxLoc(x, &lo, &hi);
        if (hi > lo) return (x - lo) / (hi - lo);
        return cv::Mat::zeros(x.size(), x.type());
    }

    double mad_calculator(const cv::Mat& img) {
        std::vector<double> v = to_vector(img);
        const double med = median(v);
        for (double& x : v) x = std::abs(x - med);
        return median(v);
    }

    cv::Mat percentile(const cv::Mat& img, double n_eme) {
        const double maxi = np_percentile(to_vector(img), n_eme);
        // Comme en Python : si maxi vaut 0, la division produit des NaN.
        cv::Mat clipped = cv::max(img, 0.0);
        clipped = cv::min(clipped, maxi);
        return clipped / maxi;
    }

    cv::Mat gray_raw(const cv::Mat& img) {
        CV_Assert(img.channels() == 3);
        cv::Mat f, g;
        img.convertTo(f, CV_32FC3);
        cv::cvtColor(f, g, cv::COLOR_RGB2GRAY);
        return g;
    }

    CostMaps compute_cost_maps(const cv::Mat& crop_a, const cv::Mat& crop_b, const CostMapParams& p) {
        if (!is_one_of(p.normalization, { "noise", "percentile" }))
            throw std::invalid_argument("normalization doit valoir 'noise' ou 'percentile'.");
        if (!is_one_of(p.color_norm, { "manhattan", "euclidian" }))
            throw std::invalid_argument("color_norm doit valoir 'manhattan' ou 'euclidian'.");
        if (!is_one_of(p.treatment, { "sigmoid", "linear", "squared", "caped" }))
            throw std::invalid_argument("treatment doit valoir sigmoid, linear, squared ou caped");
        CV_Assert(crop_a.channels() == 3 && crop_b.channels() == 3 && crop_a.size() == crop_b.size());

        cv::Mat fa, fb;
        crop_a.convertTo(fa, CV_32FC3);
        crop_b.convertTo(fb, CV_32FC3);
        const cv::Mat d_col = fa - fb;

        std::vector<cv::Mat> ch;
        cv::split(d_col, ch);

        cv::Mat e_color = cv::Mat::zeros(d_col.size(), CV_32F);
        if (p.color_norm == "manhattan") {
            for (const cv::Mat& c : ch) e_color += cv::abs(c);
            e_color /= 3.0;
        }
        else {
            for (const cv::Mat& c : ch) e_color += c.mul(c);
            cv::sqrt(e_color, e_color);
        }

        // Normalisation
        if (p.normalization == "noise") {
            const double mad = (mad_calculator(ch[0]) + mad_calculator(ch[1]) + mad_calculator(ch[2])) / 3.0;
            const double bruit_col = std::max(mad * 1.4826, 1.0);   // plancher pour ne pas diviser par quelque chose < 1
            e_color = e_color / bruit_col;
        }
        else {
            e_color = percentile(e_color, 99);
        }

        const cv::Mat d_gray = gray_raw(d_col);
        const Gradients grad = gradient(d_gray);
        const double bruit_grad = std::max((mad_calculator(grad.Gx) + mad_calculator(grad.Gy)) * 1.4826 / 2.0, 1.0);
        cv::Mat e_gradient;
        if (p.normalization == "noise") {
            e_gradient = grad.magnitude / bruit_grad;
        }
        else {
            e_gradient = percentile(grad.magnitude, 99);
            // il ne faudrait plus appliquer de sigmoïde !
        }

        // combined = w_color * e_color + (1 - w_color) * e_gradient   (en CV_64F, comme NumPy)
        cv::Mat wc;
        cv::Mat(e_color * p.w_color).convertTo(wc, CV_64F);
        cv::Mat combined = wc + (1.0 - p.w_color) * e_gradient;

        // On favorise les hautes fréquences en divisant par Gx de A et Gx de B
        const cv::Mat abs_gx_a = cv::abs(gradient(gray_raw(crop_a)).Gx);
        const cv::Mat abs_gx_b = cv::abs(gradient(gray_raw(crop_b)).Gx);
        cv::Mat texture, factor;
        cv::min(abs_gx_a, abs_gx_b, texture);
        cv::divide(bruit_grad, bruit_grad + texture, factor);   // = 1 si les gradients horizontaux sont nuls
        combined = combined.mul(factor);

        cv::Mat treated;
        if (p.treatment == "sigmoid" && p.normalization == "noise") {
            cv::Mat e;
            cv::exp(-4.0 * (combined - 3.0), e);
            cv::divide(1.0, 1.0 + e, treated);
        }
        else if (p.treatment == "squared") {
            treated = combined.mul(combined);
        }
        else if (p.treatment == "caped") {
            treated = cv::max(combined, 0.0);
            treated = cv::min(treated, 5.0);
        }
        else {
            treated = combined;
        }

        if (p.recall) {
            // Rappel vers le centre : nul sur la moitié centrale, quadratique jusqu'au bord, lam par ligne
            const int w = treated.cols;
            const double half = std::max((w - 1) / 2.0, 1.0);
            cv::Mat g(1, w, CV_64F);
            for (int x = 0; x < w; ++x) {
                const double r = std::abs(x - (w - 1) / 2.0) / half;   // 0 au centre, 1 au bord
                const double q = std::max((r - 0.5) / 0.5, 0.0);
                g.at<double>(0, x) = q * q;
            }
            treated = treated + p.lam * cv::repeat(g, treated.rows, 1);
        }

        CostMaps out;
        out.e_color = e_color;
        out.e_gradient = e_gradient;
        treated.convertTo(out.treated, CV_32F);
        return out;
    }

}  // namespace seamlib
