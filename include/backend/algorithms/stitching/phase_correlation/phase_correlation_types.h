// include/backend/algorithms/stitching/phase_correlation/phase_correlation_types.h
#pragma once

#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

// Structures échangées entre les étapes de la corrélation de phase :
// PreparationClass → AlgorithmClass → OutputClass (voir phase_correlation_matcher.cpp pour l'enchaînement).
namespace PhaseCorrelationNs {

    // Réglages de l'algorithme, remplis par l'appelant (vérifiés par PreparationClass::validateParameters).
    // Entre parenthèses : nom du paramètre dans le step phase-correlation.
    struct Parameters {
        double smoothingSigma = 3.0;          // (lissage) sigma (px) du flou gaussien de la surface de corrélation globale
        double minimumPeakRatio = 1.5;        // (seuilRapport) rapport minimal pic / second pic pour accepter une mesure
        double localSmoothingSigma = 1.5;     // (lissageLocal) sigma (px) du flou gaussien des surfaces locales
        double maximumLocalDeviation = 3.0;   // (ecartMax) écart maximal (px) entre la translation locale et la globale
        double minimumPointSpacing = 16.0;    // (espacementMin) distance minimale (px) entre deux points
        double railMargin = 0.0;              // (marge) écart maximal (px) entre la translation globale et le recouvrement
                                              // nominal (incertitude du rail, première passe) ; 0 = automatique
                                              // (voir computeRailMargin dans preparation.cpp)
        double minimumValidFraction = 0.25;   // (fractionValideMin) part minimale de pixels exploitables, communs aux deux bandes
        int maximumPointCount = 10;           // (nbPoints) nombre maximal de points renvoyés (0 = translation seule)
        int patchSize = 64;                   // (fenetre) côté (px) des patchs de corrélation locale
        int upsamplingFactor = 100;           // (facteurDft) précision du sous-pixel : 1/upsamplingFactor px
                                              // (1 = au pixel, sans raffinement)
    };

    // Paire de tuiles ramenée au cas « B en bas ou à droite de A ».
    struct OrientedPair {
        cv::Mat imageA, imageB;               // en-têtes OpenCV : les pixels ne sont pas copiés
        cv::Mat outsideMaskA, outsideMaskB;   // zones extérieures (masques 8 bits, 255 = hors de l'œuvre)
        DataNs::Direction direction;          // Bottom ou Right
        bool isSwapped;                       // true si A et B ont été échangées (B était en haut ou à gauche)
    };

    // Bandes prêtes pour la corrélation globale : bandes de recouvrement nominal (première passe)
    // ou zone commune réelle (deuxième passe).
    struct Strips {
        cv::Mat stripA, stripB;   // gris float64, NaN = vide, même taille, découpées sur la zone avec contenu
        cv::Point2d origin;       // (tx0, ty0) : position de B dans A si le décalage résiduel est nul
        double searchMargin;      // décalage résiduel maximal (px) cherché, sur chaque axe : marge du rail
                                  // (première passe) ou erreur permise à la première passe (deuxième passe)
    };

    // Résultat d'une corrélation de phase : a(y, x) ≈ b(y - shiftY, x - shiftX).
    struct Correlation {
        double shiftY, shiftX;
        double peakHeight;     // hauteur du pic sur la surface brute
        cv::Mat surface;       // surface lissée (non centrée : décalage nul en (0, 0))
        cv::Mat allowedMask;   // masque 8 bits des décalages où le pic est cherché (vide = toute la surface)
    };

    // Pourquoi la translation globale a été refusée : sert seulement à expliquer l'échec de match().
    struct Diagnostic {
        double peakRatioInMargin;        // rapport pic / second pic dans la marge du rail (celui qui a été refusé)
        cv::Point2d unconstrainedPeak;   // décalage résiduel entier (dx, dy) du meilleur pic, cherché sur toute la surface
        double unconstrainedPeakRatio;   // rapport pic / second pic de ce meilleur pic, sur toute la surface
        bool isOutsideMargin;            // ce meilleur pic dépasse la marge du rail sur au moins un axe
    };

    // Translation globale entre les deux tuiles.
    struct Measurement {
        double translationX, translationY;   // coin haut-gauche de B dans le repère de A (px)
        double peakRatio;                    // rapport pic / second pic (première passe ; NaN après la deuxième, qui
                                             // ne le mesure pas)
    };

    // Zone où A et B se superposent, une fois B placée à la translation globale arrondie.
    struct Overlap {
        cv::Mat regionA, regionB;       // même zone vue dans A et dans B, gris float64, NaN = vide
        cv::Point originInA;            // coin de la zone dans A
        cv::Point roundedTranslation;   // translation globale arrondie (tx, ty)
        int borderMargin;               // distance minimale (px) entre un candidat et le bord de la zone
    };

    // Point mesuré localement, avant conversion dans le repère des tuiles.
    struct Correspondence {
        cv::Point2f pointInA;            // centre du patch, dans le repère de Overlap::regionA
        cv::Point2d localTranslation;    // translation locale (tx, ty) mesurée autour de ce point
        float confidence;                // NCC entre les deux patchs, entre 0 et 1
    };

}
