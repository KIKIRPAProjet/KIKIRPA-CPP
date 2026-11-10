// include/backend/steps/mosaic_crop_step.h
#pragma once

#include "core/data/mosaic_files.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    // Rectangle en pixels pleine résolution, dans le repère des blocs sources.
    struct MosaicCropInput {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;

        // Dossier inexistant dont le parent existe déjà.
        std::string outputDirectory;
    };

    struct MosaicCropOutput {
        std::string key = "mosaic";
        DataNs::MosaicFiles value;
        std::string type = "MosaicFiles";
    };

    // Remplace mosaic par sa version rognée uniquement en cas de succès.
    class MosaicCropStepClass : public StepNs::StepClass {

    public:
        explicit MosaicCropStepClass(const MosaicCropInput& input = {});
        bool execute() override;
        const MosaicCropOutput& getOutput() const { return output; }

    private:
        void cropAndPublish();
        MosaicCropOutput output;

    };

}