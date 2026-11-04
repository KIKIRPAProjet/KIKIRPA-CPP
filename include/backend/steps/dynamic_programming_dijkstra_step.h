// include/backend/steps/dynamic_programming_dijkstra_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

    struct DynamicProgrammingDijkstraInput {
        std::string method = "DP";
        std::string costFunction = "color_gradient";
        int corridorRadius = 18;
    };

    struct DynamicProgrammingDijkstraOutput {
        DataNs::Seamlines seamlines;
    };

    // Calcule une couture par paire de tuiles voisines à partir de leurs pixels, positions et gains d'égalisation.
    // Les coutures produites sont exprimées en pixels de pleine résolution dans le repère de la mosaïque.
    class DynamicProgrammingDijkstraStepClass : public StepNs::StepClass {

        public:
            explicit DynamicProgrammingDijkstraStepClass(const DynamicProgrammingDijkstraInput& initialInput = {});
            bool execute() override;
            const DynamicProgrammingDijkstraOutput& getOutput() const { return output; }

        private:
            DynamicProgrammingDijkstraOutput output;

    };

}
