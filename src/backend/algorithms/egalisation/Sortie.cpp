//Sortie.cpp
#include "algorithms/egalisation/Sortie.h"
#include "algorithms/egalisation/Preparation.h"
#include <opencv2/core/utility.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

namespace stitching::egalisation {

cv::Mat Sortie::corriger(const cv::Mat& tuile, double gain, double c) {
    if (tuile.empty()) return {};
    const int canaux = tuile.channels();
    const bool flottant = tuile.depth() == CV_32F || tuile.depth() == CV_64F;
    // Entier : sortie 16 bits, valeurs ramenées sur 0-65535 (8 bits : × 257). Flottant : même type.
    const int profondeur = flottant ? tuile.depth() : CV_16U;
    const double echelle = flottant ? 1.0 : 65535.0 / Preparation::valeurMax(tuile.depth());
    const double vmax = flottant ? 1.0 : 65535.0;
    const int canauxCouleur = canaux == 4 ? 3 : canaux;   // canal alpha recopié sans correction

    const int w = tuile.cols, h = tuile.rows;
    std::vector<double> u2(w);
    for (int x = 0; x < w; ++x) {
        const double u = 2.0 * (x + 0.5) / w - 1.0;
        u2[x] = u * u;
    }

    cv::Mat sortie(tuile.size(), CV_MAKETYPE(profondeur, canaux));
    // Ligne par ligne, en parallèle : aucune copie flottante de la tuile entière
    cv::parallel_for_(cv::Range(0, h), [&](const cv::Range& lignes) {
        cv::Mat entree, resultat;
        std::vector<double> facteur(w);
        for (int y = lignes.start; y < lignes.end; ++y) {
            const double v = 2.0 * (y + 0.5) / h - 1.0;
            for (int x = 0; x < w; ++x) facteur[x] = std::exp(-c * (u2[x] + v * v)) / gain * echelle;

            tuile.row(y).convertTo(entree, CV_MAKETYPE(CV_64F, canaux));
            const double* e = entree.ptr<double>();
            resultat.create(1, w, CV_MAKETYPE(CV_64F, canaux));
            double* r = resultat.ptr<double>();
            for (int x = 0; x < w; ++x)
                for (int k = 0; k < canaux; ++k) {
                    const double val = e[x * canaux + k] * (k < canauxCouleur ? facteur[x] : echelle);
                    r[x * canaux + k] = std::min(std::max(flottant ? val : std::round(val), 0.0), vmax);
                }
            resultat.convertTo(sortie.row(y), sortie.type());
        }
    });
    return sortie;
}

std::string Sortie::rapport(const Resultat& r) {
    std::string s = "Égalisation : " + std::to_string(r.nbEchantillons) + " échantillons\n";
    auto liste = [](const std::vector<double>& g) {
        std::string t;
        for (std::size_t i = 0; i < g.size(); ++i) t += (i ? " " : "") + nombre(g[i], 4);
        return t;
    };
    s += "  gains A (gain global)   : " + liste(r.gainsA) + "\n";
    s += "  gains B (gain + champ)  : " + liste(r.gainsB) + "\n";
    s += "  champ B : c = " + nombre(r.c, 4) + " (bord " + nombre(std::exp(r.c), 3) + ", coin "
         + nombre(std::exp(2 * r.c), 3) + ")\n";
    auto ligne = [](const char* nom, const Bilan& b) {
        return std::string("  ") + nom + " écart moyen " + nombre(b.ecartMoyen, 2) + " %, variation BF "
               + nombre(b.variationBF, 2) + " %, RMS résidu " + nombre(b.rmsResidu, 2) + " %, résidu médian "
               + nombre(b.residuMedian, 2) + " %\n";
    };
    s += ligne("avant :", r.avant);
    s += ligne("A     :", r.apresA);
    s += ligne("B     :", r.apresB);
    if (!r.tuilesIsolees.empty()) {
        s += "  tuiles sans paire exploitable (gain 1, non égalisées) :";
        for (int t : r.tuilesIsolees) s += " " + std::to_string(t);
        s += "\n";
    }
    if (!r.pairesVides.empty()) {
        s += "  paires sans échantillon (pas de recouvrement exploitable) :";
        for (int k : r.pairesVides) s += " " + std::to_string(k);
        s += "\n";
    }
    return s;
}

std::string Sortie::echecSansEchantillon(std::size_t nbPaires) {
    const std::string paires = nbPaires == 1 ? "la paire n'a aucun"
                                             : "aucune des " + std::to_string(nbPaires) + " paires n'a de";
    return "égalisation : " + paires + " pixel exploitable dans son recouvrement "
           "(translations fausses, tuiles vides ou saturées, ou marge trop grande)";
}

std::string Sortie::nombre(double valeur, int decimales) {
    char tampon[64];
    std::snprintf(tampon, sizeof tampon, "%.*f", decimales, valeur);
    return tampon;
}

} // namespace stitching::egalisation
