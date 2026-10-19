#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace stitching {

enum class RunStatus { Idle, Running, Paused, Failed, Completed };

NLOHMANN_JSON_SERIALIZE_ENUM(RunStatus, {{RunStatus::Idle, "idle"},
                                         {RunStatus::Running, "running"},
                                         {RunStatus::Paused, "paused"},
                                         {RunStatus::Failed, "failed"},
                                         {RunStatus::Completed, "completed"}})

// Champ "run" du script, frère de "pipeline" :
//   { "pipeline": { "steps": [...] },
//     "run": { "status": "failed", "completedSteps": ["import", "stitching"],
//              "skip": true, "from": "", "checkpointDir": "output/checkpoints" } }
//
// Deux sortes de champs :
//  - ÉTAT (écrit par le backend après chaque step) : status, completedSteps
//  - CONSIGNES (écrites par le frontend, consommées puis remises à leur défaut au démarrage) : skip, from
//  - checkpointDir : où sont les résultats intermédiaires (relatif au dossier courant)
struct RunState {
    RunStatus status = RunStatus::Idle;
    std::vector<std::string> completedSteps;   // préfixe du pipeline, dans l'ordre
    bool skip = true;                          // true : on saute les steps déjà faits ; false : on repart de zéro
    std::string from;                          // optionnel : nom du step où (re)démarrer (doit être <= premier step non fait)
    std::string checkpointDir = "output/checkpoints";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunState, status, completedSteps, skip, from, checkpointDir)

} // namespace stitching
