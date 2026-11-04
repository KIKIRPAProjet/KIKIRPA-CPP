// include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h
#pragma once

#include "algorithms/stitching/feature_matcher.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <string>

namespace PhaseCorrelationNs {

    // Corrélation de phase : translation entre deux tuiles voisines, en deux passes, puis points de correspondance
    // raffinés localement (port de correlation_phase.py).
    //   1. première passe (OverlapEstimatorClass) : position de B au pixel près sur les bandes de recouvrement
    //      nominal ; sautée si context.estimatedTranslation est déjà fournie par le pipeline ;
    //   2. deuxième passe : translation au 1/upsamplingFactor px sur la zone commune réelle à cette position.
    // Seul header à inclure depuis le pipeline ; le détail est dans phase_correlation/ :
    //   PreparationClass (images de travail) → AlgorithmClass (mesures) → OutputClass (DataNs::Matches).
    //
    // Paramètres : structure Parameters (phase_correlation_types.h), déjà remplie par l'appelant. Chaque champ a une
    // valeur par défaut ; std::invalid_argument à la construction si une valeur est invalide. Entre parenthèses : nom
    // du paramètre dans le step phase-correlation.
    //   smoothingSigma (lissage)                3.0   sigma (px) du flou gaussien de la surface de corrélation globale
    //   minimumPeakRatio (seuilRapport)         1.5   rapport minimal pic / second pic pour accepter une mesure
    //   maximumPointCount (nbPoints)            10    nombre maximal de points renvoyés (0 = translation seule)
    //   patchSize (fenetre)                     64    côté (px) des patchs de corrélation locale
    //   localSmoothingSigma (lissageLocal)      1.5   sigma (px) du flou gaussien des surfaces locales
    //   maximumLocalDeviation (ecartMax)        3.0   écart maximal (px) entre la translation locale et la globale
    //   minimumPointSpacing (espacementMin)     16    distance minimale (px) entre deux points
    //   railMargin (marge)                      0     écart maximal (px) au recouvrement nominal, première passe
    //                                                 (0 = 5 % de la tuile, au moins 48 px)
    //   minimumValidFraction (fractionValideMin) 0.25 part minimale de pixels exploitables communs aux deux bandes
    //   upsamplingFactor (facteurDft)           100   précision du sous-pixel : 1/upsamplingFactor px
    class PhaseCorrelationMatcherClass : public StitchingNs::FeatureMatcherClass {

        public:
            // Sans argument : toutes les valeurs par défaut.
            explicit PhaseCorrelationMatcherClass(const Parameters& initialParameters = {});

            // false si la translation ne peut pas être mesurée (tuiles vides ou de tailles différentes,
            // pas de contenu commun, pic hors de la marge du rail ou ambigu, context.estimatedTranslation fausse de
            // plus de quelques px) : la raison est dans getFailureReason().
            // true : matches.translationX, matches.translationY valides, points éventuellement vides.
            bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                       DataNs::Matches& matches) override;

            // Pourquoi le dernier match() a renvoyé false, avec la correction à apporter quand elle est connue
            // (ex. « Increase the rail margin (Parameters::railMargin ... >= 520 px) »). Vide si le dernier match() a
            // réussi.
            const std::string& getFailureReason() const { return failureReason; }

        private:
            Parameters parameters;                                     // vérifiés une seule fois, dans le constructeur
            OverlapEstimationNs::OverlapEstimatorClass overlapEstimator;   // utilisée quand context.estimatedTranslation
                                                                       // n'est pas fournie
            std::string failureReason;                                 // message du dernier échec de match()

    };

}
