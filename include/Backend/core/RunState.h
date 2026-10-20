#pragma once
#include "core/Types.h"
#include <stdexcept>
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

inline void validateRunStateJson(const Json& value) {
    if (!value.is_object())
        throw std::invalid_argument("field 'run' must be an object");

    if (value.contains("status")) {
        if (!value.at("status").is_string())
            throw std::invalid_argument("field 'run.status' must be a string");
        const std::string status = value.at("status").get<std::string>();
        if (status != "idle" && status != "running" && status != "paused" &&
            status != "failed" && status != "completed")
            throw std::invalid_argument("field 'run.status' has unknown value '" + status + "'");
    }
    if (value.contains("completedSteps")) {
        const Json& completed = value.at("completedSteps");
        if (!completed.is_array())
            throw std::invalid_argument("field 'run.completedSteps' must be an array");
        for (size_t i = 0; i < completed.size(); ++i) {
            if (!completed.at(i).is_string())
                throw std::invalid_argument("field 'run.completedSteps[" + std::to_string(i) +
                                            "]' must be a string");
        }
    }
    if (value.contains("skip") && !value.at("skip").is_boolean())
        throw std::invalid_argument("field 'run.skip' must be a boolean");
    if (value.contains("from") && !value.at("from").is_string())
        throw std::invalid_argument("field 'run.from' must be a string");
    if (value.contains("checkpointDir") && !value.at("checkpointDir").is_string())
        throw std::invalid_argument("field 'run.checkpointDir' must be a string");
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunState, status, completedSteps, skip, from, checkpointDir)

} // namespace stitching
