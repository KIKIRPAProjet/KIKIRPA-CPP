// include/backend/core/pipeline/saved_state.h
#pragma once

#include "core/pipeline/pipeline.h"

#include <cstddef>

namespace PipelineNs {

    // État sauvegardé d'un pipeline, pour l'afficher dans l'interface (tâche 16).
    // Renvoie le nombre de steps de tête déjà faits (runState.completedSteps), de même nom, dont le checkpoint est
    // encore valide : le prochain run reprendra au step d'index countSavedSteps() (égal au nombre de steps si tout
    // est fait ; 0 sans dossier de checkpoints). Même règle que la reprise de PipelineClass::run.
    std::size_t countSavedSteps(const PipelineClass& pipeline);

}
