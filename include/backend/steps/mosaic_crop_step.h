// include/backend/steps/mosaic_crop_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    struct MosaicCropInput {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        // Dossier nouveau ou résultat identifié de mosaic-crop ; parent existant.
        std::string outputDirectory;
    };

    struct MosaicCropOutput {
        std::string key = "mosaic";
        DataNs::Mosaic value;
        std::string type = "Mosaic";
    };

    // Rogne les trois TIFF après fusion, avant export. La clé mosaic est remplacée
    // uniquement en cas de succès ; un échec conserve la mosaïque d'entrée.
    class MosaicCropStepClass : public StepNs::StepClass {

    public:
        explicit MosaicCropStepClass(const MosaicCropInput& input = {});
        bool execute() override;
        // Suit les trois fichiers produits pour invalider un checkpoint périmé.
        StepNs::Json externalInputsFingerprint() const override;
        const MosaicCropOutput& getOutput() const { return output; }

    private:
        bool cropAndPublish();
        MosaicCropOutput output;

    };

}