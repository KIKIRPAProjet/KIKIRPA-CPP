// include/backend/steps/absolute_pos_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
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
        // 2e sortie : l'écart de chaque paire après ajustement (clé, valeur et type, comme pour les positions).
        std::string residualsKey = "pairResiduals";
        std::vector<DataNs::PairResidual> residuals;   // écart de chaque paire après ajustement, dans l'ordre des paires utilisées
        std::string residualsType = "PairResiduals";
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
