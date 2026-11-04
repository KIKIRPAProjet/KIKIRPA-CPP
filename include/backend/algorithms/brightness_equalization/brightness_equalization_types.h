// include/backend/algorithms/brightness_equalization/brightness_equalization_types.h
#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>

// Structures échangées entre les étapes de l'égalisation de luminosité :
// PreparationClass → AlgorithmClass → OutputClass (voir brightness_equalizer.cpp pour l'enchaînement).
namespace BrightnessEqualizationNs {

    // Méthode de correction appliquée aux tuiles
    enum class CorrectionMethod {
        GlobalGain,     // A : un gain par tuile (Brown & Lowe, 2007)
        GainAndField    // B : A + champ d'éclairage exp(c · r²) commun à toutes les tuiles
    };

    // Réglages de l'algorithme, remplis par l'appelant (vérifiés par PreparationClass::validateParameters)
    struct Parameters {
        int reductionFactor = 8;      // réduction de la tuile pour l'estimation (1/8) : le modèle n'a que n + 1 paramètres
        double blurSigma = 1.5;       // sigma (px de l'image réduite) du flou gaussien appliqué avant l'estimation
        int borderMargin = 40;        // px (pleine résolution) ignorés au bord de chaque tuile
        double lowThreshold = 0.03;   // luminance (fraction de la dynamique) en dessous : pixel trop sombre, exclu
        double highThreshold = 0.97;  // luminance au-dessus : pixel saturé (reflet), exclu
        int iterationCount = 6;       // itérations des moindres carrés repondérés (IRLS)
        double huberThreshold = 1.5;  // seuil des poids de Huber, en écarts-types robustes (MAD × 1,4826)
        double gaugeWeight = 1e3;     // poids de la contrainte « moyenne des log g = 0 »
    };

    // Ce que l'estimation garde d'une tuile : sa luminance réduite (quelques Mo au lieu de plusieurs centaines)
    struct ReducedTile {
        cv::Mat luminance;   // luminance dans [0, 1], CV_32F, réduite puis floutée
        cv::Size fullSize;   // taille de la tuile en pleine résolution
    };

    // Paire de tuiles voisines, avec la translation mesurée par le stitching (corrélation de phase, SIFT…)
    struct NeighbourPair {
        int tileIndexA = 0, tileIndexB = 0;   // indices des tuiles dans le vecteur passé à BrightnessEqualizerClass::estimate
        cv::Point2d translation;              // coin haut-gauche de B dans le repère de A (px, pleine résolution)
    };

    // Échantillons d'une paire : un par pixel valide du recouvrement, sur les images réduites
    struct PairSamples {
        int tileIndexA = 0, tileIndexB = 0;
        std::vector<float> squaredRadiiA, squaredRadiiB;   // position normalisée du pixel dans A et dans B
                                                           // (0 au centre, 1 au bord)
        std::vector<float> logDifferences;                 // log(I_A) − log(I_B)
    };

    // Solution des moindres carrés
    struct Solution {
        std::vector<double> logGains;   // log g de chaque tuile (moyenne nulle)
        double fieldStrength = 0.0;     // force c du champ d'éclairage (0 pour la méthode A)
    };

    // Qualité de l'égalisation sur les recouvrements, en % (plus petit = mieux)
    struct QualityReport {
        double meanDifference = 0.0;          // différence absolue moyenne entre les deux tuiles, en % de la dynamique
        double lowFrequencyVariation = 0.0;   // variation basse fréquence du rapport de luminosité (5e → 95e centile)
        double residualRms = 0.0;             // RMS de l'écart de luminosité restant entre pixels correspondants
        double medianResidual = 0.0;          // écart absolu médian restant entre pixels correspondants
    };

    // Résultat de l'estimation : de quoi corriger chaque tuile avec A ou B
    struct EqualizationResult {
        std::vector<double> gainsA;       // méthode A : gain de chaque tuile
        std::vector<double> gainsB;       // méthode B : gain de chaque tuile
        double fieldStrength = 0.0;       // méthode B : champ V(x, y) = exp(c · r²), r² = u² + v², u, v ∈ [−1, 1]
        std::vector<int> isolatedTiles;   // tuiles sans aucune paire exploitable : gain 1, non égalisées
        std::vector<int> emptyPairs;      // indices (dans le vecteur de paires) des paires sans échantillon valide
        std::size_t sampleCount = 0;
        QualityReport before, afterA, afterB;

        double getGain(int tileIndex, CorrectionMethod method) const {
            return method == CorrectionMethod::GlobalGain ? gainsA.at(tileIndex) : gainsB.at(tileIndex);
        }
        double getFieldStrength(CorrectionMethod method) const {
            return method == CorrectionMethod::GlobalGain ? 0.0 : fieldStrength;
        }
    };

}
