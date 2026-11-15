// include/backend/steps/overlap_estimation_step.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    constexpr float DEFAULT_OVERLAP_FRACTION = 0.2f;

    // Paramètres de l'estimation des zones de recouvrement.
    struct OverlapEstimationInput {
        float overlap = DEFAULT_OVERLAP_FRACTION;
        PhaseCorrelationNs::Parameters parameters;
    };

    // Estimations publiées sous la clé overlap-estimates.
    struct OverlapEstimationOutput {
        std::string key = "overlapEstimates";
        DataNs::OverlapEstimates value;
        std::string type = "OverlapEstimates";
    };

    // Estime la translation et la zone commune des voisines droite et dessous de chaque tuile.
    // Les paires impossibles à mesurer sont signalées dans getResult()["failures"].
    class OverlapEstimationStepClass : public StepNs::StepClass {

        public:
            explicit OverlapEstimationStepClass(const OverlapEstimationInput& input = {});
            bool execute() override;
            const OverlapEstimationOutput& getOutput() const { return output; }

        private:
            bool estimatePairs(float overlap, const PhaseCorrelationNs::Parameters& parameters);
            OverlapEstimationOutput output;

    };

}
