// test_egalisation.cpp
// Vérification de Egalisation (voir docs/algorithms/egalisation/Egalisation.md §6).
//
// Tests à vérité exacte (GoogleTest, ctest -L unit) : 6 tuiles (grille 2 × 3) découpées dans une texture
// générée, à des positions connues (décalages perpendiculaires compris), puis multipliées par des gains et un
// champ d'éclairage connus. Tuiles 8 et 16 bits mélangées, gris puis couleur, une tuile plus haute que les autres.
// On vérifie les gains et le champ retrouvés, la tuile corrigée, puis les cas limites.

#include "algorithms/egalisation/Egalisation.h"
#include "algorithms/egalisation/Sortie.h"
#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace stitching::tests {

using egalisation::Egalisation;
using egalisation::Methode;
using egalisation::PaireVoisine;
using egalisation::Resultat;
using egalisation::TuileReduite;

constexpr int    LIGNES = 2, COLONNES = 3;
constexpr int    COTE = 1600;              // px : tuiles carrées (sauf une, plus haute)
constexpr int    PAS = 1328;               // px : pas de la grille, recouvrement de 17 %
constexpr int    SURPLUS_HAUTEUR = 50;     // px ajoutés à la tuile (1, 0), comme la tuile 01 du dataset 05
constexpr double CHAMP = -0.12;            // champ d'éclairage appliqué : 0,887 au bord, 0,787 dans un coin
const double     GAINS[LIGNES * COLONNES] = {1.30, 1.00, 0.70, 1.10, 0.95, 0.85};
constexpr double TOLERANCE_GAIN_B = 0.01;  // écart relatif maximal des gains B à la vérité
constexpr double TOLERANCE_GAIN_A = 0.03;  // A ne modélise pas le champ : un peu moins précis
constexpr double TOLERANCE_CHAMP  = 0.005; // écart maximal de c à la vérité
constexpr double ECART_MAX_APRES  = 0.5;   // % : écart moyen dans les recouvrements après B

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool condition, const std::string& message) {
    EXPECT_TRUE(condition) << message;
}

std::string format(const char* modele, double a, double b = 0, double c = 0) {
    char tampon[256];
    std::snprintf(tampon, sizeof tampon, modele, a, b, c);
    return tampon;
}

// Texture multi-échelle dans [0,25 ; 0,75] (assez de contenu à toutes les échelles, pas de saturation)
cv::Mat textureGeneree(cv::Size taille, int graine) {
    cv::RNG rng(graine);
    cv::Mat somme = cv::Mat::zeros(taille, CV_32F);
    for (double sigma : {2.0, 5.0, 12.0, 30.0, 60.0}) {
        cv::Mat bruit(taille, CV_32F);
        rng.fill(bruit, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(bruit, bruit, cv::Size(0, 0), sigma);
        cv::Scalar moyenne, ecart;
        cv::meanStdDev(bruit, moyenne, ecart);
        somme += (bruit - moyenne[0]) / ecart[0];
    }
    double mini, maxi;
    cv::minMaxLoc(somme, &mini, &maxi);
    return 0.25 + 0.5 * (somme - mini) / (maxi - mini);
}

struct Jeu {
    std::vector<cv::Mat> tuiles;              // tuiles « acquises » (gain et champ appliqués)
    std::vector<cv::Point2d> positions;       // coin haut-gauche de chaque tuile dans la scène
    std::vector<std::pair<int, int>> voisins; // (gauche, droite) et (haut, bas)
};

// Grille LIGNES × COLONNES ; tuile i = ligne * COLONNES + colonne. Tuiles d'indice pair en 8 bits.
Jeu genererJeu(int canaux, int graine) {
    const cv::Size scene(PAS * (COLONNES - 1) + COTE + 100, PAS * (LIGNES - 1) + COTE + SURPLUS_HAUTEUR + 100);
    const cv::Mat texture = textureGeneree(scene, graine);
    cv::RNG rng(graine + 100);
    Jeu jeu;
    for (int l = 0; l < LIGNES; ++l)
        for (int c = 0; c < COLONNES; ++c) {
            const int i = l * COLONNES + c;
            // Décalage perpendiculaire inconnu de l'algorithme, comme sur un vrai rail
            const cv::Point position(30 + c * PAS + rng.uniform(-8, 9), 30 + l * PAS + rng.uniform(-8, 9));
            const int hauteur = (l == 1 && c == 0) ? COTE + SURPLUS_HAUTEUR : COTE;
            cv::Mat t = texture(cv::Rect(position, cv::Size(COTE, hauteur))).clone();
            for (int y = 0; y < t.rows; ++y)
                for (int x = 0; x < t.cols; ++x) {
                    const double u = 2.0 * (x + 0.5) / t.cols - 1.0, v = 2.0 * (y + 0.5) / t.rows - 1.0;
                    t.at<float>(y, x) *= static_cast<float>(GAINS[i] * std::exp(CHAMP * (u * u + v * v)));
                }
            cv::Mat sortie;
            const bool huitBits = i % 2 == 0;
            t.convertTo(sortie, huitBits ? CV_8U : CV_16U, huitBits ? 255.0 : 65535.0);
            if (canaux == 3) {
                // Couleur : même luminance répartie avec une teinte (B, G, R) fixe
                std::vector<cv::Mat> plans;
                for (double teinte : {0.6, 0.85, 1.0}) {
                    cv::Mat p;
                    sortie.convertTo(p, sortie.type(), teinte);
                    plans.push_back(p);
                }
                cv::merge(plans, sortie);
            }
            jeu.tuiles.push_back(sortie);
            jeu.positions.push_back(position);
        }
    for (int l = 0; l < LIGNES; ++l)
        for (int c = 0; c < COLONNES; ++c) {
            const int i = l * COLONNES + c;
            if (c + 1 < COLONNES) jeu.voisins.push_back({i, i + 1});
            if (l + 1 < LIGNES) jeu.voisins.push_back({i, i + COLONNES});
        }
    return jeu;
}

// Gains vrais ramenés à une moyenne des log nulle (la même jauge que l'algorithme)
std::vector<double> gainsVrais() {
    double somme = 0;
    for (double g : GAINS) somme += std::log(g);
    std::vector<double> g;
    for (double x : GAINS) g.push_back(x / std::exp(somme / (LIGNES * COLONNES)));
    return g;
}

void testerJeu(const std::string& nom, int canaux) {
    std::cout << "\n=== " << nom << " ===\n";
    const Jeu jeu = genererJeu(canaux, canaux == 1 ? 7 : 11);
    Egalisation eg;
    std::vector<TuileReduite> reduites;
    for (const auto& t : jeu.tuiles) reduites.push_back(eg.reduire(t));
    const auto paires = Egalisation::pairesDepuisPositions(jeu.positions, jeu.voisins);
    const auto r = eg.estimer(reduites, paires);
    verifier(r.has_value(), "estimation réussie" + (r ? std::string() : " : " + eg.raisonEchec()));
    if (!r) return;
    std::cout << egalisation::Sortie::rapport(*r);

    const auto vrais = gainsVrais();
    double pireA = 0, pireB = 0;
    for (int i = 0; i < LIGNES * COLONNES; ++i) {
        pireA = std::max(pireA, std::abs(r->gainsA[i] / vrais[i] - 1));
        pireB = std::max(pireB, std::abs(r->gainsB[i] / vrais[i] - 1));
    }
    verifier(pireB < TOLERANCE_GAIN_B, format("gains B à %.2f %% près de la vérité (tolérance %.1f %%)", 100 * pireB, 100 * TOLERANCE_GAIN_B));
    verifier(pireA < TOLERANCE_GAIN_A, format("gains A à %.2f %% près de la vérité (tolérance %.1f %%)", 100 * pireA, 100 * TOLERANCE_GAIN_A));
    verifier(std::abs(r->c - CHAMP) < TOLERANCE_CHAMP, format("champ c = %.4f (vrai %.4f)", r->c, CHAMP));
    verifier(r->apresB.ecartMoyen < ECART_MAX_APRES && r->apresB.ecartMoyen < r->apresA.ecartMoyen
             && r->apresA.ecartMoyen < r->avant.ecartMoyen,
             format("écart moyen : avant %.2f %% > A %.2f %% > B %.2f %%", r->avant.ecartMoyen, r->apresA.ecartMoyen, r->apresB.ecartMoyen));
    verifier(r->tuilesIsolees.empty() && r->pairesVides.empty(), "toutes les tuiles et paires sont utilisées");

    // Tuile corrigée : type, taille, teinte conservée, et même luminosité que sa voisine dans le recouvrement
    const cv::Mat c0 = Egalisation::corriger(jeu.tuiles[0], *r, 0, Methode::GainEtChamp);
    const cv::Mat c1 = Egalisation::corriger(jeu.tuiles[1], *r, 1, Methode::GainEtChamp);
    verifier(c0.depth() == CV_16U && c0.channels() == canaux && c0.size() == jeu.tuiles[0].size(),
             "tuile corrigée en 16 bits, même taille et mêmes canaux (entrée 8 bits)");
    const cv::Point t = jeu.positions[1] - jeu.positions[0];
    const cv::Rect zoneA(t.x + 100, std::max(0, t.y) + 100, COTE - t.x - 200, 600);
    const cv::Rect zoneB = zoneA - t;
    const double mA = cv::mean(c0(zoneA))[0], mB = cv::mean(c1(zoneB))[0];
    verifier(std::abs(mA / mB - 1) < 0.005, format("recouvrement 0-1 après B : luminosités moyennes %.0f et %.0f", mA, mB));
    if (canaux == 3) {
        const cv::Scalar m = cv::mean(c0);
        verifier(std::abs(m[0] / m[2] - 0.6) < 0.01 && std::abs(m[1] / m[2] - 0.85) < 0.01,
                 format("teinte conservée : B/R = %.3f, G/R = %.3f", m[0] / m[2], m[1] / m[2]));
    }
}

void testerCasLimites() {
    std::cout << "\n=== Cas limites ===\n";
    bool leve = false;
    try {
        Egalisation::Parametres p;
        p.seuilBas = 0.9;
        p.seuilHaut = 0.5;
        Egalisation eg(p);
    } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "paramètre invalide (seuilBas >= seuilHaut) : std::invalid_argument");

    Egalisation eg;
    const Jeu jeu = genererJeu(1, 3);
    std::vector<TuileReduite> reduites;
    for (const auto& t : jeu.tuiles) reduites.push_back(eg.reduire(t));

    // estimer() d'abord, puis le message : dans une même expression, l'ordre d'évaluation n'est pas garanti
    auto echec = [&](const std::vector<TuileReduite>& t, const std::vector<PaireVoisine>& p, const std::string& cas) {
        const bool vide = !eg.estimer(t, p);
        verifier(vide && !eg.raisonEchec().empty(), cas + " : échec expliqué (" + eg.raisonEchec() + ")");
    };
    echec({reduites[0]}, {}, "une seule tuile");
    echec(reduites, {{0, 9, {0, 0}}}, "indice de paire hors limites");
    echec(reduites, {{0, 1, {50000, 0}}}, "aucun recouvrement");

    // Tuile 5 retirée de toutes les paires : gain exactement 1, signalée
    auto paires = Egalisation::pairesDepuisPositions(jeu.positions, jeu.voisins);
    std::vector<PaireVoisine> sans5;
    for (const auto& p : paires)
        if (p.a != 5 && p.b != 5) sans5.push_back(p);
    sans5.push_back({0, 1, {50000, 0}});   // paire sans recouvrement : ignorée, signalée
    const auto r = eg.estimer(reduites, sans5);
    verifier(r && r->tuilesIsolees == std::vector<int>{5} && r->gainsB[5] == 1.0 && r->gainsA[5] == 1.0,
             "tuile sans paire : gain 1, signalée dans tuilesIsolees");
    verifier(r && r->pairesVides == std::vector<int>{static_cast<int>(sans5.size()) - 1},
             "paire sans recouvrement : ignorée, signalée dans pairesVides");

    // Réduction depuis un niveau de pyramide : même résultat que depuis la pleine résolution
    cv::Mat petite;
    cv::resize(jeu.tuiles[1], petite, cv::Size(), 1.0 / 8, 1.0 / 8, cv::INTER_AREA);
    const TuileReduite a = eg.reduire(jeu.tuiles[1]), b = eg.reduire(petite, jeu.tuiles[1].size());
    double ecart = cv::norm(a.lum, b.lum, cv::NORM_INF);
    verifier(a.lum.size() == b.lum.size() && ecart < 1e-3, format("réduction depuis un niveau 1/8 : écart max %.1e", ecart));
}

TEST(Egalisation, TuilesEnNiveauxDeGris) { testerJeu("Tuiles en niveaux de gris (8 et 16 bits)", 1); }
TEST(Egalisation, TuilesCouleur) { testerJeu("Tuiles couleur BGR (8 et 16 bits)", 3); }
TEST(Egalisation, CasLimites) { testerCasLimites(); }

} // namespace stitching::tests
