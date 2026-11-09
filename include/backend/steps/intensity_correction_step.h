// include/backend/steps/intensity_correction_step.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"
#include "core/data/intensity_correction.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    // Valeurs initiales des paramètres du step.
    struct IntensityCorrectionInput {
        std::string method = "gain-and-field";   // "gain" : un gain par tuile ; "gain-and-field" : gains et champ d'éclairage
        // La réduction est fixée à 1/8 ; les autres valeurs sont celles de l'algorithme existant.
        BrightnessEqualizationNs::Parameters algorithmParameters;
    };

    // Sortie du step.
    struct IntensityCorrectionOutput {
        std::string key = "intensityCorrection";
        DataNs::IntensityCorrection value;
        std::string type = "IntensityCorrection";
    };

    // Estime, par moindres carrés robustes, la correction de luminosité de chaque tuile à partir des recouvrements
    // mesurés. Le step ne modifie aucun pixel : la correction s'applique à l'assemblage
    // (voir docs/steps/IntensityCorrectionStep.md).
    //
    // Nom : "intensity-correction".
    // Paramètres : method (string) ; blurSigma, lowThreshold, highThreshold, huberThreshold, gaugeWeight (float) ;
    //   borderMargin, iterationCount (int). Un paramètre absent garde sa valeur par défaut.
    // Entrées : "Tiles" (clé "tiles") et "PairMatches" (clé "matches").
    // Sortie : "IntensityCorrection" (clé "intensityCorrection").
    //
    // Les paires inutilisables (tuile inconnue, paire d'une tuile avec elle-même, translation non finie, doublon,
    // recouvrement sans échantillon) sont listées dans getResult()["failures"] sans faire échouer le step.
    // Le step échoue (cause dans getResult()["error"]) si une entrée manque, si method est inconnue, si un paramètre
    // est invalide, si une image est illisible ou ne correspond pas à ses métadonnées, ou si aucune paire n'est
    // exploitable. Après un échec, aucune ancienne correction ne reste publiée.
    class IntensityCorrectionStepClass : public StepNs::StepClass {

        public:
            explicit IntensityCorrectionStepClass(const IntensityCorrectionInput& input = {});
            bool execute() override;
            const IntensityCorrectionOutput& getOutput() const { return output; }

        private:
            // Relit les paramètres de la configuration. Lève std::invalid_argument si l'un est hors limites ou non fini.
            IntensityCorrectionInput readInput() const;
            IntensityCorrectionOutput output;
    };

}
