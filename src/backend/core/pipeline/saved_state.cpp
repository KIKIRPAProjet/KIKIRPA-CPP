// src/backend/core/pipeline/saved_state.cpp
#include "core/pipeline/saved_state.h"

#include "core/checkpoint/checkpoint.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"

#include <cstddef>
#include <string>

namespace PipelineNs {

    std::size_t countSavedSteps(const PipelineClass& pipeline) {
        const RunState& runState = pipeline.getRunState();
        const CheckpointNs::CheckpointClass checkpoint(runState.checkpointDirectory);
        const auto& steps = pipeline.getSteps();

        // On avance tant que le step est déjà fait, porte le même nom et a un checkpoint valide (nom, paramètres et
        // entrées inchangés) ; dès qu'un step ne l'est pas, lui et tous les suivants sont à refaire.
        std::size_t savedStepCount = 0;
        while (savedStepCount < runState.completedSteps.size() && savedStepCount < steps.size() &&
               runState.completedSteps[savedStepCount] == steps[savedStepCount]->getConfigJson().value("name", std::string()) &&
               checkpoint.valid(savedStepCount, *steps[savedStepCount]))
            ++savedStepCount;
        return savedStepCount;
    }

}
