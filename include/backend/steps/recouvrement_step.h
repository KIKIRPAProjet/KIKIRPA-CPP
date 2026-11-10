// include/backend/steps/recouvrement_step.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    constexpr float DEFAULT_OVERLAP = 0.2f;

    // Paramètres de l'estimation des zones de recouvrement.
    struct RecouvrementInput {
        float overlap = DEFAULT_OVERLAP;
        PhaseCorrelationNs::Parameters parameters;
    };

    // Estimations publiées sous la clé overlap-estimates.
    struct RecouvrementOutput {
        std::string key = "overlap-estimates";
        DataNs::OverlapEstimates value;
        std::string type = "OverlapEstimates";
    };

    // Estime la translation et la zone commune des voisines droite et dessous de chaque tuile.
    // Les paires impossibles à mesurer sont signalées dans getResult()["failures"].
    class RecouvrementStepClass : public StepNs::StepClass {

        public:
            explicit RecouvrementStepClass(const RecouvrementInput& input = {});
            bool execute() override;
            const RecouvrementOutput& getOutput() const { return output; }

        private:
            bool estimatePairs(float overlap, const PhaseCorrelationNs::Parameters& parameters);
            RecouvrementOutput output;

    };

}
