// include/backend/steps/absolute_pos_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>
#include <vector>

namespace StepsNs {

    struct AbsolutePosInput {
        // -1 : tuile de plus petit identifiant numérique ; sinon identifiant TileClass explicite.
        int referenceTileId = -1;
    };

    struct AbsolutePosOutput {
        std::string key = "positions";
        std::vector<DataNs::TilePositionData> value;
        std::string type = "Positions";
    };

    // Transforme les translations relatives de matches en positions globales ; ne relit aucune image.
    class AbsolutePosStepClass : public StepNs::StepClass {

        public:
            explicit AbsolutePosStepClass(const AbsolutePosInput& input = {});
            bool execute() override;
            const AbsolutePosOutput& getOutput() const { return output; }

        private:
            AbsolutePosOutput output;
    };

}
