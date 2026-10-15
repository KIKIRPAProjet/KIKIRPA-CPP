#include "algorithms/seamline/cost_map.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace seamlib {

    namespace {

        // Copie toutes les valeurs (tous canaux) dans un vecteur de doubles.
        // La conversion écrit directement dans le vecteur : pas de cv::Mat intermédiaire.
        std::vector<double> to_vector(const cv::Mat& m) {
            const cv::Mat flat = m.reshape(1, 1);
            std::vector<double> v(flat.total());
            cv::Mat view(1, static_cast<int>(v.size()), CV_64F, v.data());
            flat.convertTo(view, CV_64F);   // même taille et même type : OpenCV écrit dans `view` sans réallouer
            return v;
        }

        // Équivalent de np.median. Réordonne `v` sur place au lieu d'en faire une copie.
        double median_inplace(std::vector<double>& v) {
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

        // |Gx| du gris d'une image RGB ou déjà grise. Seul Gx sert au facteur de texture : on ne calcule ni Gy ni la norme.
        cv::Mat abs_sobel_x(const cv::Mat& img) {
            cv::Mat gx;
            cv::Sobel(gray_raw(img), gx, CV_64F, 1, 0, 3);   // mêmes réglages que gradient()
            cv::absdiff(gx, cv::Scalar::all(0), gx);         // = cv::abs(gx), sur place
            return gx;
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
        // Un seul vecteur : la médiane le réordonne, ce qui ne change ni la médiane ni les écarts.
        std::vector<double> v = to_vector(img);
        const double med = median_inplace(v);
        for (double& x : v) x = std::abs(x - med);
        return median_inplace(v);
    }

    cv::Mat percentile(const cv::Mat& img, double n_eme) {
        const double maxi = np_percentile(to_vector(img), n_eme);
        // Comme en Python : si maxi vaut 0, la division produit des NaN.
        cv::Mat clipped = cv::max(img, 0.0);
        clipped = cv::min(clipped, maxi);
        return clipped / maxi;
    }

    cv::Mat gray_raw(const cv::Mat& img) {
        CV_Assert(img.channels() == 1 || img.channels() == 3);
        cv::Mat g;
        if (img.channels() == 1) {               // déjà en gris (ex. IRR) : rien à mélanger
            img.convertTo(g, CV_32F);
            return g;
        }
        if (img.depth() == CV_32F) {             // déjà en float : pas de copie intermédiaire
            cv::cvtColor(img, g, cv::COLOR_RGB2GRAY);
            return g;
        }
        cv::Mat f;
        img.convertTo(f, CV_32FC3);
        cv::cvtColor(f, g, cv::COLOR_RGB2GRAY);
        return g;
    }

    // Mémoire : chaque matrice intermédiaire vit dans un bloc { } ou est libérée par release()
    // dès sa dernière utilisation. Les calculs et leur ordre sont ceux de la version d'origine.
    CostMaps compute_cost_maps(const cv::Mat& crop_a, const cv::Mat& crop_b, const CostMapParams& p) {
        if (!is_one_of(p.normalization, { "noise", "percentile" }))
            throw std::invalid_argument("normalization doit valoir 'noise' ou 'percentile'.");
        if (!is_one_of(p.color_norm, { "manhattan", "euclidian" }))
            throw std::invalid_argument("color_norm doit valoir 'manhattan' ou 'euclidian'.");
        if (!is_one_of(p.treatment, { "sigmoid", "linear", "squared", "caped" }))
            throw std::invalid_argument("treatment doit valoir sigmoid, linear, squared ou caped");
        // 1 canal (gris, ex. IRR) ou 3 canaux (RGB). En gris, l'écart de couleur se réduit à |A − B|.
        CV_Assert((crop_a.channels() == 1 || crop_a.channels() == 3) && crop_a.channels() == crop_b.channels()
            && crop_a.size() == crop_b.size());

        const bool noise = (p.normalization == "noise");
        const int n_ch = crop_a.channels();

        // --- 1a. Écart de couleur --------------------------------------------------------------
        // d_col = A − B, calculé dans le buffer de A : deux images float au lieu de trois.
        cv::Mat d_col;
        crop_a.convertTo(d_col, CV_MAKETYPE(CV_32F, n_ch));
        {
            cv::Mat fb;
            crop_b.convertTo(fb, CV_MAKETYPE(CV_32F, n_ch));
            d_col -= fb;
        }   // fb libéré

        // Un canal à la fois (au lieu de split, qui garde les 3 en mémoire) : norme et MAD.
        cv::Mat e_color = cv::Mat::zeros(d_col.size(), CV_32F);
        double mad_sum = 0.0;
        {
            cv::Mat c;
            for (int k = 0; k < n_ch; ++k) {
                cv::extractChannel(d_col, c, k);
                if (p.color_norm == "manhattan")
                    e_color += cv::abs(c);
                else
                    e_color += c.mul(c);
                if (noise) mad_sum += mad_calculator(c);
            }
        }   // canal libéré
        if (p.color_norm == "manhattan")
            e_color /= double(n_ch);
        else
            cv::sqrt(e_color, e_color);

        // --- 2. Normalisation de la couleur ---------------------------------------------------
        if (noise) {
            const double mad = mad_sum / double(n_ch);   // moyenne des MAD par canal
            const double bruit_col = std::max(mad * 1.4826, 1.0);   // plancher pour ne pas diviser par quelque chose < 1
            e_color = e_color / bruit_col;
        }
        else {
            e_color = percentile(e_color, 99);
        }

        // --- 1b. Écart de gradient + normalisation -------------------------------------------
        double bruit_grad = 1.0;
        cv::Mat e_gradient;
        {
            Gradients grad;
            {
                const cv::Mat d_gray = gray_raw(d_col);
                d_col.release();                                     // dernière utilisation de A − B
                grad = gradient(d_gray);
            }   // d_gray libéré
            bruit_grad = std::max((mad_calculator(grad.Gx) + mad_calculator(grad.Gy)) * 1.4826 / 2.0, 1.0);
            grad.Gx.release();                                       // Gx et Gy ne servaient qu'au bruit
            grad.Gy.release();
            if (noise)
                e_gradient = grad.magnitude / bruit_grad;
            else
                e_gradient = percentile(grad.magnitude, 99);
        }   // norme du gradient libérée

        // --- 3. Combinaison ------------------------------------------------------------------
        // combined = w_color * e_color + (1 - w_color) * e_gradient   (en CV_64F, comme NumPy)
        cv::Mat combined;
        {
            cv::Mat wc;
            cv::Mat(e_color * p.w_color).convertTo(wc, CV_64F);
            combined = wc + (1.0 - p.w_color) * e_gradient;
        }   // wc libéré

        // --- 4. Texture : on favorise les hautes fréquences en divisant par Gx de A et Gx de B --
        {
            cv::Mat texture = abs_sobel_x(crop_a);
            cv::min(texture, abs_sobel_x(crop_b), texture);
            cv::Mat factor;
            cv::divide(bruit_grad, bruit_grad + texture, factor);   // = 1 si les gradients horizontaux sont nuls
            texture.release();
            combined = combined.mul(factor);
        }   // factor libéré

        // --- 5. Traitement -------------------------------------------------------------------
        cv::Mat treated;
        if (p.treatment == "sigmoid" && noise) {
            cv::Mat e;
            cv::exp(-4.0 * (combined - 3.0), e);
            combined.release();
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
        combined.release();

        // --- 6. Rappel vers le centre --------------------------------------------------------
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
