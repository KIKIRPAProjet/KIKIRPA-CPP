// src/backend/core/pipeline/saved_state.cpp
#include "core/pipeline/saved_state.h"

#include "core/io/unicode_path.h"

#include <string>

namespace PipelineNs {

    std::size_t countSavedSteps(const std::vector<std::unique_ptr<StepNs::StepClass>>& steps, const RunState& runState,
                                const CheckpointNs::CheckpointClass& checkpoint) {
        // On avance tant que le step est déjà fait, porte le même nom et a un checkpoint valide (nom, paramètres et
        // entrées inchangés) ; dès qu'un step ne l'est pas, lui et tous les suivants sont à refaire.
        std::size_t savedStepCount = 0;
        while (savedStepCount < runState.completedSteps.size() && savedStepCount < steps.size()) {
            const StepNs::StepClass& step = *steps[savedStepCount];
            const std::string name = step.getConfigJson().is_object() ? step.getConfigJson().value("name", "") : "";
            if (runState.completedSteps[savedStepCount] != name || !checkpoint.valid(savedStepCount, step)) break;
            ++savedStepCount;
        }
        return savedStepCount;
    }

    std::size_t countSavedSteps(const PipelineClass& pipeline) {
        const RunState& runState = pipeline.getRunState();
        const CheckpointNs::CheckpointClass checkpoint(IONs::pathFromUtf8(runState.checkpointDirectory));
        return countSavedSteps(pipeline.getSteps(), runState, checkpoint);
    }

}
