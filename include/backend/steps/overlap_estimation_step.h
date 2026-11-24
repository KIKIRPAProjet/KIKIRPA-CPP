// include/backend/steps/overlap_estimation_step.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    constexpr float DEFAULT_OVERLAP_FRACTION = 0.2f;
    // Marge automatique du rail (railMargin = 0) : 10 % de la tuile dans le sens du déplacement, le double de celle de la
    // corrélation de phase. Cette passe est grossière (au pixel près) et les steps d'appariement affinent ensuite.
    constexpr double OVERLAP_ESTIMATION_MARGIN_FRACTION = 0.10;

    // Paramètres de l'estimation des zones de recouvrement.
    struct OverlapEstimationInput {
        float overlap = DEFAULT_OVERLAP_FRACTION;   // recouvrement nominal (paires horizontales si verticalOverlap > 0)
        float verticalOverlap = 0.f;                // recouvrement nominal des paires verticales ; 0 = comme overlap
        bool overlapCalibration = true;             // recalage du recouvrement nominal par direction (calibrateOverlap)
        PhaseCorrelationNs::Parameters parameters;  // automaticRailMarginFraction est remplacé par
                                                    // OVERLAP_ESTIMATION_MARGIN_FRACTION
    };

    // Estimations publiées sous la clé overlapEstimates.
    struct OverlapEstimationOutput {
        std::string key = "overlapEstimates";
        DataNs::OverlapEstimates value;
        std::string type = "OverlapEstimates";
    };

    // Estime la translation et la zone commune des voisines droite et dessous de chaque tuile.
    // Les paires impossibles à mesurer sont signalées dans getResult()["failures"] : elles n'arrêtent pas le step.
    //
    // Nom : "overlap-estimation".   Paramètres : overlap, verticalOverlap, smoothingSigma, minimumPeakRatio, railMargin
    // (px ; 0 = OVERLAP_ESTIMATION_MARGIN_FRACTION de la tuile, au moins 48 px), minimumValidFraction (type "float") ;
    // overlapCalibration (type "bool").
    // Entrée : "Tiles" (clé "tiles").   Sortie : "OverlapEstimates" (clé "overlapEstimates").
    //
    // Étapes de execute() : lecture des tuiles, contrôle de overlap et verticalOverlap, recherche des paires voisines
    // (voisines droite et dessous, dédoublonnées), estimation de chacune à son recouvrement nominal, recalage (comme
    // phase-correlation : voir getResult()["overlapCalibration"]), publication des paires mesurées.
    class OverlapEstimationStepClass : public StepNs::StepClass {

        public:
            explicit OverlapEstimationStepClass(const OverlapEstimationInput& input = {});
            bool execute() override;
            const OverlapEstimationOutput& getOutput() const { return output; }

        private:
            bool estimatePairs(const OverlapEstimationInput& input);
            OverlapEstimationOutput output;

    };

}
