// include/backend/steps/dynamic_programming_dijkstra_step.h
#pragma once

#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>
#include <vector>

namespace StepsNs {

    struct DynamicProgrammingDijkstraInput {
        std::string method = "DP";
        std::string costFunction = "color_gradient";
        int corridorRadius = DynamicProgrammingDijkstraNs::DEFAULT_CORRIDOR_RADIUS;
    };

    struct DynamicProgrammingDijkstraOutput {
        DataNs::Seamlines seamlines;
        std::vector<DataNs::TileTerritory> territories;
    };

    // Entrées lues par execute() (paramètres et données du DataStore), définies dans le .cpp.
    struct DynamicProgrammingDijkstraData;

    // Calcule une couture par paire de tuiles voisines et les territoires pour la fusion.
    // Les coutures restent dans le repère absolu ; les territoires utilisent le repère
    // entier normalisé de FusionStep.
    class DynamicProgrammingDijkstraStepClass : public StepNs::StepClass {

        public:
            explicit DynamicProgrammingDijkstraStepClass(const DynamicProgrammingDijkstraInput& initialInput = {});
            bool execute() override;
            const DynamicProgrammingDijkstraOutput& getOutput() const { return output; }

        private:
            DynamicProgrammingDijkstraOutput output;

            // Lit les paramètres et les entrées du DataStore. Renvoie la cause d'un échec, vide si tout est lu.
            std::string readInputs(DynamicProgrammingDijkstraData& inputs) const;

    };

}
