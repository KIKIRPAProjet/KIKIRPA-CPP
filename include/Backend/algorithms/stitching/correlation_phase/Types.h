#pragma once
#include "core/Types.h"

// Structures échangées entre les étapes de la corrélation de phase :
// Preparation → Algorithme → Sortie (voir CorrelationPhase.cpp pour l'enchaînement).
namespace stitching::matching::correlation_phase {

// Réglages de l'algorithme, remplis par l'appelant (vérifiés par Preparation::verifierParametres)
struct Parametres {
    double lissage       = 3.0;   // sigma (px) du flou gaussien de la surface de corrélation globale
    double seuilRapport  = 1.5;   // rapport minimal pic / second pic pour accepter une mesure
    double lissageLocal  = 1.5;   // sigma (px) du flou gaussien des surfaces locales
    double ecartMax      = 3.0;   // écart maximal (px) entre la translation locale et la globale
    double espacementMin = 16.0;  // distance minimale (px) entre deux points
    double marge         = 0.0;   // écart maximal (px) entre la translation globale et le recouvrement nominal
                                  // (incertitude du rail) ; 0 = automatique (voir Preparation::margeRail)
    double fractionValideMin = 0.25;  // part minimale de pixels exploitables, communs aux deux bandes
    int    nbPoints      = 10;    // nombre maximal de points renvoyés (0 = translation seule)
    int    fenetre       = 64;    // côté (px) des patchs de corrélation locale
    int    facteurDft    = 100;   // précision du sous-pixel : 1/facteurDft px
};

// Paire de tuiles ramenée au cas « B en bas ou à droite de A »
struct PaireOrientee {
    cv::Mat A, B;                 // en-têtes OpenCV : les pixels ne sont pas copiés
    cv::Mat extA, extB;           // zones extérieures (masques 8 bits, 255 = hors de l'œuvre)
    Direction position;           // Bottom ou Right
    bool inversee;                // true si A et B ont été échangées (B était en haut ou à gauche)
};

// Bandes de recouvrement prêtes pour la corrélation globale
struct Bandes {
    cv::Mat a, b;                 // gris float64, NaN = vide, même taille, découpées sur la zone avec contenu
    cv::Point2d origine;          // (tx0, ty0) : position de B dans A si le décalage résiduel est nul
    double marge;                 // décalage résiduel maximal (px) permis par le rail, sur chaque axe
};

// Résultat d'une corrélation de phase : a(y, x) ≈ b(y - dy, x - dx)
struct Correlation {
    double dy, dx;
    double pic;                   // hauteur du pic sur la surface brute
    cv::Mat surface;              // surface lissée (non centrée : décalage nul en (0, 0))
    cv::Mat autorise;             // masque 8 bits des décalages où le pic est cherché (vide = toute la surface)
};

// Pourquoi la translation globale a été refusée : sert seulement à expliquer l'échec de match()
struct Diagnostic {
    double rapportMarge;          // rapport pic / second pic dans la marge du rail (celui qui a été refusé)
    cv::Point2d picLibre;         // décalage résiduel entier (dx, dy) du meilleur pic, cherché sur toute la surface
    double rapportLibre;          // rapport pic / second pic de ce meilleur pic, sur toute la surface
    bool horsMarge;               // ce meilleur pic dépasse la marge du rail sur au moins un axe
};

// Translation globale entre les deux tuiles
struct Mesure {
    double tx, ty;                // coin haut-gauche de B dans le repère de A (px)
    double rapport;               // rapport pic / second pic
};

// Zone où A et B se superposent, une fois B placée à la translation globale arrondie
struct Recouvrement {
    cv::Mat a, b;                 // même zone vue dans A et dans B, gris float64, NaN = vide
    cv::Point origineA;           // coin de la zone dans A
    cv::Point decalage;           // translation globale arrondie (tx, ty)
    int marge;                    // distance minimale (px) entre un candidat et le bord de la zone
};

// Point mesuré localement, avant conversion dans le repère des tuiles
struct Correspondance {
    cv::Point2f pointA;           // centre du patch, dans le repère de Recouvrement::a
    cv::Point2d translation;      // translation locale (tx, ty) mesurée autour de ce point
    float confiance;              // NCC entre les deux patchs, entre 0 et 1
};

} // namespace stitching::matching::correlation_phase
