// include/backend/steps/intensity_correction_step.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"
#include "core/data/intensity_correction.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    struct IntensityCorrectionInput {
        std::string method = "gain-and-field";
        // La réduction est fixée à 1/8 ; les autres valeurs sont celles de l'algorithme existant.
        BrightnessEqualizationNs::Parameters algorithmParameters;
    };

    struct IntensityCorrectionOutput {
        std::string key = "intensityCorrection";
        DataNs::IntensityCorrection value;
        std::string type = "IntensityCorrection";
    };

    // Estime des gains et un champ d'éclairage, sans modifier les pixels des tuiles sources.
    class IntensityCorrectionStepClass : public StepNs::StepClass {

        public:
            explicit IntensityCorrectionStepClass(const IntensityCorrectionInput& input = {});
            bool execute() override;
            const IntensityCorrectionOutput& getOutput() const { return output; }

        private:
            IntensityCorrectionInput readInput() const;
            IntensityCorrectionOutput output;
    };

}
