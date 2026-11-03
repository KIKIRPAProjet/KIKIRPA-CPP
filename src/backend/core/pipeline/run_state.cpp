// src/backend/core/pipeline/run_state.cpp
#include "core/pipeline/run_state.h"

#include <stdexcept>
#include <string>

namespace PipelineNs {

    // Contrôle chaque champ avant la conversion automatique, pour des messages précis et pour refuser un statut inconnu.
    RunState RunState::fromJson(const Json& runJson) {
        // Tous les champs sont facultatifs : un champ absent garde sa valeur par défaut. Seul un champ présent est contrôlé.
        if (!runJson.is_object()) throw std::invalid_argument("field 'run' must be an object");

        // status : doit être l'un des textes de l'enum RunStatus (voir NLOHMANN_JSON_SERIALIZE_ENUM dans run_state.h).
        if (runJson.contains("status")) {
            const Json& statusJson = runJson.at("status");
            if (!statusJson.is_string()) throw std::invalid_argument("field 'run.status' must be a string");
            const std::string statusName = statusJson.get<std::string>();
            if (statusName != "idle" && statusName != "running" && statusName != "paused" && statusName != "failed" && statusName != "completed")
                throw std::invalid_argument("field 'run.status' has unknown value '" + statusName + "'");
        }
        // completedSteps : tableau de noms de steps.
        if (runJson.contains("completedSteps")) {
            const Json& completedStepsJson = runJson.at("completedSteps");
            if (!completedStepsJson.is_array()) throw std::invalid_argument("field 'run.completedSteps' must be an array");
            for (const Json& completedStepName : completedStepsJson)
                if (!completedStepName.is_string()) throw std::invalid_argument("field 'run.completedSteps' must contain strings");
        }
        // skipCompletedSteps : booléen ; startFromStep et checkpointDirectory : textes.
        if (runJson.contains("skipCompletedSteps") && !runJson.at("skipCompletedSteps").is_boolean())
            throw std::invalid_argument("field 'run.skipCompletedSteps' must be a boolean");
        for (const char* field : {"startFromStep", "checkpointDirectory"})
            if (runJson.contains(field) && !runJson.at(field).is_string())
                throw std::invalid_argument(std::string("field 'run.") + field + "' must be a string");

        // Le JSON est valide : la conversion automatique (macro de run_state.h) ne peut plus échouer.
        return runJson.get<RunState>();
    }

}
