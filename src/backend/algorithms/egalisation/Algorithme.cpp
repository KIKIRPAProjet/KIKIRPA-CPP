//Algorithme.cpp
#include "algorithms/egalisation/Algorithme.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace stitching::egalisation {

Echantillons Algorithme::echantillons(const TuileReduite& A, const TuileReduite& B, const PaireVoisine& paire,
                                      const Parametres& p) {
    Echantillons e;
    e.a = paire.a;
    e.b = paire.b;
    const cv::Mat& la = A.lum;
    const cv::Mat& lb = B.lum;
    // Échelle réelle de chaque image réduite (l'arrondi de la taille la rend un peu différente de p.reduction)
    const double sxA = A.pleine.width / static_cast<double>(la.cols), syA = A.pleine.height / static_cast<double>(la.rows);
    const double sxB = B.pleine.width / static_cast<double>(lb.cols), syB = B.pleine.height / static_cast<double>(lb.rows);
    const double m = p.marge;
    const float bas = static_cast<float>(p.seuilBas), haut = static_cast<float>(p.seuilHaut);

    for (int y = 0; y < la.rows; ++y) {
        // Centre du pixel réduit, en pleine résolution dans A, puis le même point dans B
        const double yA = (y + 0.5) * syA;
        const double yB = yA - paire.translation.y;
        if (yA <= m || yA >= A.pleine.height - m || yB <= m || yB >= B.pleine.height - m) continue;
        const double vB = yB / syB - 0.5;                  // ligne (fractionnaire) dans l'image réduite de B
        const int y0 = static_cast<int>(std::floor(vB));
        if (y0 < 0 || y0 + 1 >= lb.rows) continue;
        const float fy = static_cast<float>(vB - y0);
        const float* ligneA = la.ptr<float>(y);
        const float* b0 = lb.ptr<float>(y0);
        const float* b1 = lb.ptr<float>(y0 + 1);

        for (int x = 0; x < la.cols; ++x) {
            const float iA = ligneA[x];
            if (iA <= bas || iA >= haut) continue;         // trop sombre ou saturé (reflet)
            const double xA = (x + 0.5) * sxA;
            const double xB = xA - paire.translation.x;
            if (xA <= m || xA >= A.pleine.width - m || xB <= m || xB >= B.pleine.width - m) continue;
            const double uB = xB / sxB - 0.5;
            const int x0 = static_cast<int>(std::floor(uB));
            if (x0 < 0 || x0 + 1 >= lb.cols) continue;
            const float fx = static_cast<float>(uB - x0);
            // Interpolation bilinéaire dans B (comme cv::remap INTER_LINEAR dans le notebook)
            const float iB = (1 - fy) * ((1 - fx) * b0[x0] + fx * b0[x0 + 1]) + fy * ((1 - fx) * b1[x0] + fx * b1[x0 + 1]);
            if (iB <= bas || iB >= haut) continue;
            e.r2a.push_back(static_cast<float>(r2(xA, yA, A.pleine)));
            e.r2b.push_back(static_cast<float>(r2(xB, yB, B.pleine)));
            e.d.push_back(std::log(iA) - std::log(iB));
        }
    }
    return e;
}

Solution Algorithme::resoudre(const std::vector<Echantillons>& echantillons, int nbTuiles, bool avecChamp,
                              const Parametres& p) {
    const int n = nbTuiles;
    const int m = n + (avecChamp ? 1 : 0);             // inconnues : log g_0 … log g_(n−1) [, c]
    std::size_t total = 0;
    std::vector<bool> relie(n, false);
    for (const auto& e : echantillons) {
        total += e.d.size();
        if (!e.d.empty()) relie[e.a] = relie[e.b] = true;
    }

    // Chaque échantillon est une équation  log g_a − log g_b + c · (r²_a − r²_b) = d.
    // On résout les équations normales (m × m) au lieu du système complet (millions de lignes) : chaque ligne
    // n'a que 2 ou 3 coefficients non nuls, on les accumule directement.
    std::vector<double> poids(total, 1.0);             // poids IRLS (carré du poids de ligne du notebook)
    std::vector<double> absResidu(total);
    Solution s;
    s.logGains.assign(n, 0.0);

    for (int it = 0; it < p.iterations; ++it) {
        cv::Mat N = cv::Mat::zeros(m, m, CV_64F), B = cv::Mat::zeros(m, 1, CV_64F);
        double* nn = N.ptr<double>();
        double* bb = B.ptr<double>();
        std::size_t k = 0;
        for (const auto& e : echantillons) {
            const int a = e.a, b = e.b;
            for (std::size_t i = 0; i < e.d.size(); ++i, ++k) {
                const double q = poids[k], d = e.d[i];
                nn[a * m + a] += q;  nn[b * m + b] += q;
                nn[a * m + b] -= q;  nn[b * m + a] -= q;
                bb[a] += q * d;      bb[b] -= q * d;
                if (avecChamp) {
                    const double dr = static_cast<double>(e.r2a[i]) - e.r2b[i];
                    nn[a * m + n] += q * dr;  nn[n * m + a] += q * dr;
                    nn[b * m + n] -= q * dr;  nn[n * m + b] -= q * dr;
                    nn[n * m + n] += q * dr * dr;
                    bb[n] += q * dr * d;
                }
            }
        }
        // Contrainte de jauge : poidsJauge · (log g_0 + … + log g_(n−1)) = 0. Sans elle, tous les gains
        // pourraient être multipliés par une même constante.
        const double j2 = p.poidsJauge * p.poidsJauge;
        for (int i = 0; i < n; ++i)
            for (int jj = 0; jj < n; ++jj) nn[i * m + jj] += j2;
        // Tuile sans aucun échantillon : log g = 0 imposé (gain 1, tuile non égalisée), sinon indéterminé
        for (int i = 0; i < n; ++i)
            if (!relie[i]) nn[i * m + i] += j2;

        cv::Mat X;
        cv::solve(N, B, X, cv::DECOMP_SVD);
        for (int i = 0; i < n; ++i) s.logGains[i] = X.at<double>(i);
        s.c = avecChamp ? X.at<double>(n) : 0.0;

        // Résidus et nouveaux poids de Huber : les grands écarts (reflets, erreurs de recalage) pèsent moins
        k = 0;
        for (const auto& e : echantillons)
            for (std::size_t i = 0; i < e.d.size(); ++i, ++k) {
                const double dr = static_cast<double>(e.r2a[i]) - e.r2b[i];
                absResidu[k] = std::abs(e.d[i] - (s.logGains[e.a] - s.logGains[e.b] + s.c * dr));
            }
        std::vector<double> tri(absResidu);
        const double echelle = centile(tri, 50.0) * ECHELLE_MAD;
        if (echelle <= 0) break;                       // données exactes : rien à repondérer
        for (std::size_t i = 0; i < total; ++i)
            poids[i] = std::min(1.0, p.huber * echelle / std::max(absResidu[i], RESIDU_MIN));
    }
    return s;
}

void Algorithme::residus(const std::vector<Echantillons>& echantillons, const Solution& s, Bilan& bilan) {
    std::vector<double> ecarts;
    double somme2 = 0.0;
    for (const auto& e : echantillons)
        for (std::size_t i = 0; i < e.d.size(); ++i) {
            double r = e.d[i];
            if (!s.logGains.empty())
                r -= s.logGains[e.a] - s.logGains[e.b] + s.c * (static_cast<double>(e.r2a[i]) - e.r2b[i]);
            const double pct = 100.0 * (std::exp(r) - 1.0);   // écart de luminosité restant, en %
            somme2 += pct * pct;
            ecarts.push_back(std::abs(pct));
        }
    if (ecarts.empty()) return;
    bilan.rmsResidu = std::sqrt(somme2 / ecarts.size());
    bilan.residuMedian = centile(ecarts, 50.0);
}

void Algorithme::mesurer(const std::vector<TuileReduite>& tuiles, const std::vector<PaireVoisine>& paires,
                         const Solution& s, Bilan& bilan) {
    std::vector<cv::Mat> lum(tuiles.size());
    for (std::size_t i = 0; i < tuiles.size(); ++i)
        lum[i] = s.logGains.empty() ? tuiles[i].lum : corrigerReduite(tuiles[i].lum, std::exp(s.logGains[i]), s.c);

    const cv::Mat noyauErosion = cv::Mat::ones(EROSION_MESURE, EROSION_MESURE, CV_8U);
    double sommeEcarts = 0.0, sommePlages = 0.0;
    int nbEcarts = 0, nbPlages = 0;
    for (const auto& pr : paires) {
        const cv::Mat& la = lum[pr.a];
        const cv::Mat& lb = lum[pr.b];
        const double sxA = tuiles[pr.a].pleine.width / static_cast<double>(la.cols);
        const double syA = tuiles[pr.a].pleine.height / static_cast<double>(la.rows);
        const double sxB = tuiles[pr.b].pleine.width / static_cast<double>(lb.cols);
        const double syB = tuiles[pr.b].pleine.height / static_cast<double>(lb.rows);
        // B dans le repère réduit de A (pixels hors de B : −1, exclus ensuite)
        const cv::Mat M = (cv::Mat_<double>(2, 3) << sxB / sxA, 0, pr.translation.x / sxA,
                                                     0, syB / syA, pr.translation.y / syA);
        cv::Mat bDansA;
        cv::warpAffine(lb, bDansA, M, la.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(-1));

        cv::Mat ok = (bDansA > BAS_MESURE) & (bDansA < HAUT_MESURE) & (la > BAS_MESURE) & (la < HAUT_MESURE);
        cv::erode(ok, ok, noyauErosion);
        if (cv::countNonZero(ok) == 0) continue;

        // Écart moyen : différence absolue moyenne entre les deux tuiles, en % de la dynamique
        cv::Mat diff;
        cv::absdiff(la, bDansA, diff);
        sommeEcarts += 100.0 * cv::mean(diff, ok)[0];
        ++nbEcarts;

        // Variation basse fréquence : rapport log lissé sur une grande échelle, écart entre 5e et 95e centiles
        cv::Mat logA, logB, r;
        cv::log(cv::max(la, 1e-6), logA);
        cv::log(cv::max(bDansA, 1e-6), logB);
        r = logA - logB;
        r.setTo(0, ~ok);
        cv::Mat okf, num, den;
        ok.convertTo(okf, CV_32F, 1.0 / 255);
        cv::GaussianBlur(r, num, cv::Size(NOYAU_BF, NOYAU_BF), SIGMA_BF);
        cv::GaussianBlur(okf, den, cv::Size(NOYAU_BF, NOYAU_BF), SIGMA_BF);
        std::vector<double> v;
        for (int y = 0; y < ok.rows; ++y) {
            const uchar* o = ok.ptr<uchar>(y);
            const float* nu = num.ptr<float>(y);
            const float* de = den.ptr<float>(y);
            for (int x = 0; x < ok.cols; ++x)
                if (o[x] && de[x] > POIDS_MIN_BF) v.push_back(nu[x] / std::max(de[x], 1e-6f));
        }
        if (!v.empty()) {
            const double p5 = centile(v, 5.0), p95 = centile(v, 95.0);
            sommePlages += 100.0 * (std::exp(p95) - std::exp(p5));
        }
        ++nbPlages;
    }
    bilan.ecartMoyen = nbEcarts ? sommeEcarts / nbEcarts : 0.0;
    bilan.variationBF = nbPlages ? sommePlages / nbPlages : 0.0;
}

cv::Mat Algorithme::corrigerReduite(const cv::Mat& lum, double gain, double c) {
    cv::Mat sortie(lum.size(), CV_32F);
    const int w = lum.cols, h = lum.rows;
    std::vector<double> u2(w);
    for (int x = 0; x < w; ++x) {
        const double u = 2.0 * (x + 0.5) / w - 1.0;
        u2[x] = u * u;
    }
    for (int y = 0; y < h; ++y) {
        const double v = 2.0 * (y + 0.5) / h - 1.0;
        const float* src = lum.ptr<float>(y);
        float* dst = sortie.ptr<float>(y);
        for (int x = 0; x < w; ++x)
            dst[x] = static_cast<float>(std::clamp(src[x] * std::exp(-c * (u2[x] + v * v)) / gain, 0.0, 1.0));
    }
    return sortie;
}

double Algorithme::r2(double x, double y, cv::Size forme) {
    const double u = 2.0 * x / forme.width - 1.0;
    const double v = 2.0 * y / forme.height - 1.0;
    return u * u + v * v;
}

double Algorithme::centile(std::vector<double>& v, double q) {
    if (v.empty()) return 0.0;
    // Interpolation linéaire entre les deux rangs voisins, comme np.percentile
    const double rang = q / 100.0 * (v.size() - 1);
    const std::size_t bas = static_cast<std::size_t>(std::floor(rang));
    const std::size_t haut = std::min(bas + 1, v.size() - 1);
    std::nth_element(v.begin(), v.begin() + bas, v.end());
    const double vBas = v[bas];
    if (haut == bas) return vBas;
    const double vHaut = *std::min_element(v.begin() + bas + 1, v.end());
    return vBas + (rang - bas) * (vHaut - vBas);
}

} // namespace stitching::egalisation
