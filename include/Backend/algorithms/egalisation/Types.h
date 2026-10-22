#pragma once
#include <opencv2/core.hpp>
#include <cstddef>
#include <vector>

// Structures échangées entre les étapes de l'égalisation de luminosité :
// Preparation → Algorithme → Sortie (voir Egalisation.cpp pour l'enchaînement).
namespace stitching::egalisation {

// Méthode de correction appliquée aux tuiles
enum class Methode {
    GainGlobal,      // A : un gain par tuile (Brown & Lowe, 2007)
    GainEtChamp      // B : A + champ d'éclairage exp(c · r²) commun à toutes les tuiles
};

// Réglages de l'algorithme, remplis par l'appelant (vérifiés par Preparation::verifierParametres)
struct Parametres {
    int    reduction  = 8;      // réduction de la tuile pour l'estimation (1/8) : le modèle n'a que n + 1 paramètres
    double flou       = 1.5;    // sigma (px de l'image réduite) du flou gaussien appliqué avant l'estimation
    int    marge      = 40;     // px (pleine résolution) ignorés au bord de chaque tuile
    double seuilBas   = 0.03;   // luminance (fraction de la dynamique) en dessous : pixel trop sombre, exclu
    double seuilHaut  = 0.97;   // luminance au-dessus : pixel saturé (reflet), exclu
    int    iterations = 6;      // itérations des moindres carrés repondérés (IRLS)
    double huber      = 1.5;    // seuil des poids de Huber, en écarts-types robustes (MAD × 1,4826)
    double poidsJauge = 1e3;    // poids de la contrainte « moyenne des log g = 0 »
};

// Ce que l'estimation garde d'une tuile : sa luminance réduite (quelques Mo au lieu de plusieurs centaines)
struct TuileReduite {
    cv::Mat lum;                // luminance dans [0, 1], CV_32F, réduite puis floutée
    cv::Size pleine;            // taille de la tuile en pleine résolution
};

// Paire de tuiles voisines, avec la translation mesurée par le stitching (corrélation de phase, SIFT…)
struct PaireVoisine {
    int a = 0, b = 0;           // indices des tuiles dans le vecteur passé à Egalisation::estimer
    cv::Point2d translation;    // coin haut-gauche de B dans le repère de A (px, pleine résolution)
};

// Échantillons d'une paire : un par pixel valide du recouvrement, sur les images réduites
struct Echantillons {
    int a = 0, b = 0;
    std::vector<float> r2a, r2b;   // position normalisée du pixel dans A et dans B (0 au centre, 1 au bord)
    std::vector<float> d;          // log(I_A) − log(I_B)
};

// Solution des moindres carrés
struct Solution {
    std::vector<double> logGains;  // log g de chaque tuile (moyenne nulle)
    double c = 0.0;                // force du champ d'éclairage (0 pour la méthode A)
};

// Qualité de l'égalisation sur les recouvrements, en % (plus petit = mieux)
struct Bilan {
    double ecartMoyen    = 0.0;    // différence absolue moyenne entre les deux tuiles, en % de la dynamique
    double variationBF   = 0.0;    // variation basse fréquence du rapport de luminosité (5e → 95e centile)
    double rmsResidu     = 0.0;    // RMS de l'écart de luminosité restant entre pixels correspondants
    double residuMedian  = 0.0;    // écart absolu médian restant entre pixels correspondants
};

// Résultat de l'estimation : de quoi corriger chaque tuile avec A ou B
struct Resultat {
    std::vector<double> gainsA;      // méthode A : gain de chaque tuile
    std::vector<double> gainsB;      // méthode B : gain de chaque tuile
    double c = 0.0;                  // méthode B : champ V(x, y) = exp(c · r²), r² = u² + v², u, v ∈ [−1, 1]
    std::vector<int> tuilesIsolees;  // tuiles sans aucune paire exploitable : gain 1, non égalisées
    std::vector<int> pairesVides;    // indices (dans le vecteur de paires) des paires sans échantillon valide
    std::size_t nbEchantillons = 0;
    Bilan avant, apresA, apresB;

    double gain(int tuile, Methode m) const { return m == Methode::GainGlobal ? gainsA.at(tuile) : gainsB.at(tuile); }
    double champ(Methode m) const { return m == Methode::GainGlobal ? 0.0 : c; }
};

} // namespace stitching::egalisation
