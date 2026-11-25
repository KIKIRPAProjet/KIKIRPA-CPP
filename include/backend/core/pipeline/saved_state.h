// include/backend/core/pipeline/saved_state.h
#pragma once

#include "core/checkpoint/checkpoint.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"
#include "core/step/step.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace PipelineNs {

    // Règle de reprise, seule source pour PipelineClass::run (resolveStart) et pour l'interface (tâche 16).
    // Renvoie le nombre de steps de tête déjà faits (runState.completedSteps), de même nom, dont le checkpoint est
    // encore valide : le prochain run reprendra au step de cet index (égal au nombre de steps si tout est fait).
    std::size_t countSavedSteps(const std::vector<std::unique_ptr<StepNs::StepClass>>& steps, const RunState& runState,
                                const CheckpointNs::CheckpointClass& checkpoint);

    // Même chose avec les steps, l'état et le dossier de checkpoints du pipeline (0 sans dossier de checkpoints).
    std::size_t countSavedSteps(const PipelineClass& pipeline);

}
