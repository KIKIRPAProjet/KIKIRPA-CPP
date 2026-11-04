// include/backend/steps/intensity_correction_step.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalizer.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    // Valeurs initiales des paramètres du step.
    struct IntensityCorrectionInput {
        std::string method = "gain-and-field";   // "gain" : un gain par tuile ; "gain-and-field" : gains et champ d'éclairage
        BrightnessEqualizationNs::Parameters parameters;   // réglages de l'algorithme (reduction est imposée par le step)
    };

    // Sortie du step.
    struct IntensityCorrectionOutput {
        DataNs::IntensityCorrection correction;
    };

    // Estime, par moindres carrés robustes, la correction de luminosité de chaque tuile à partir des recouvrements
    // mesurés. Le step ne modifie aucun pixel : la correction s'applique à l'assemblage
    // (voir docs/steps/IntensityCorrectionStep.md).
    //
    // Nom : "intensity-correction".
    // Paramètres : method (string) ; blurSigma, lowThreshold, highThreshold, huberThreshold, gaugeWeight (float) ;
    //   borderMargin, iterationCount (int). Un paramètre absent garde sa valeur par défaut.
    // Entrées : "Tiles" (clé "tiles") et "PairMatches" (clé "matches").   Sortie : "IntensityCorrection" (clé "intensityCorrection").
    //
    // Échecs : le step échoue (cause dans getResult()["error"]) si une entrée manque, si method est inconnue, si un
    //   paramètre est invalide, si une image est illisible, si un identifiant de tuile des matches est inconnu, ou si
    //   l'estimation est impossible (moins de 2 tuiles, aucune paire exploitable).
    class IntensityCorrectionStepClass : public StepNs::StepClass {

    public:
        explicit IntensityCorrectionStepClass(const IntensityCorrectionInput& initialInput = {});
        bool execute() override;
        const IntensityCorrectionOutput& getOutput() const { return output; }

    private:
        IntensityCorrectionOutput output;

        // Relit les paramètres de la configuration. Renvoie false si l'un a un mauvais type (cause dans result).
        bool readInput(IntensityCorrectionInput& input);

    };

}