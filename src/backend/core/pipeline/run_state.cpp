// src/backend/core/pipeline/run_state.cpp
#include "core/pipeline/run_state.h"

#include <stdexcept>
#include <string>

namespace PipelineNs {

    namespace {

        void validateStatus(const Json& runJson) {
            if (runJson.contains("status")) {
                const Json& statusJson = runJson.at("status");
                if (!statusJson.is_string())
                    throw std::invalid_argument("field 'run.status' must be a string");
                const std::string statusName = statusJson.get<std::string>();
                if (statusName != "idle" && statusName != "running" &&
                    statusName != "paused" && statusName != "failed" &&
                    statusName != "completed")
                    throw std::invalid_argument("field 'run.status' has unknown value '" +
                                                statusName + "'");
            }
        }

        void validateCompletedSteps(const Json& runJson) {
            if (!runJson.contains("completedSteps"))
                return;
            const Json& completedStepsJson = runJson.at("completedSteps");
            if (!completedStepsJson.is_array())
                throw std::invalid_argument("field 'run.completedSteps' must be an array");
            for (const Json& completedStepName : completedStepsJson)
                if (!completedStepName.is_string())
                    throw std::invalid_argument("field 'run.completedSteps' must contain strings");
        }

        void validateStringFields(const Json& runJson) {
            if (runJson.contains("skipCompletedSteps") &&
                !runJson.at("skipCompletedSteps").is_boolean())
                throw std::invalid_argument("field 'run.skipCompletedSteps' must be a boolean");
            for (const char* field : {"startFromStep", "checkpointDirectory", "failedStep", "error"})
                if (runJson.contains(field) && !runJson.at(field).is_string())
                    throw std::invalid_argument(std::string("field 'run.") + field +
                                                "' must be a string");
        }

    }

    // Valide les champs avant conversion automatique pour refuser les statuts inconnus et les types JSON incorrects.
    RunState RunState::fromJson(const Json& runJson) {
        if (!runJson.is_object())
            throw std::invalid_argument("field 'run' must be an object");
        validateStatus(runJson);
        validateCompletedSteps(runJson);
        validateStringFields(runJson);
        if (runJson.contains("stepResults") && !runJson.at("stepResults").is_object())
            throw std::invalid_argument("field 'run.stepResults' must be an object");

        return runJson.get<RunState>();
    }

}
