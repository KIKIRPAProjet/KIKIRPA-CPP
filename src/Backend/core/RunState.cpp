// RunState.cpp
#include "core/RunState.h"
#include <stdexcept>

namespace stitching {

RunState parseRunState(const Json& run) {
    if (!run.is_object()) throw std::invalid_argument("field 'run' must be an object");
    if (run.contains("status")) {
        const Json& s = run.at("status");
        if (!s.is_string()) throw std::invalid_argument("field 'run.status' must be a string");
        const std::string v = s.get<std::string>();
        if (v != "idle" && v != "running" && v != "paused" && v != "failed" && v != "completed")
            throw std::invalid_argument("field 'run.status' has unknown value '" + v + "'");
    }
    if (run.contains("completedSteps")) {
        const Json& c = run.at("completedSteps");
        if (!c.is_array()) throw std::invalid_argument("field 'run.completedSteps' must be an array");
        for (const Json& name : c)
            if (!name.is_string()) throw std::invalid_argument("field 'run.completedSteps' must contain strings");
    }
    if (run.contains("skip") && !run.at("skip").is_boolean())
        throw std::invalid_argument("field 'run.skip' must be a boolean");
    for (const char* field : {"from", "checkpointDir"})
        if (run.contains(field) && !run.at(field).is_string())
            throw std::invalid_argument(std::string("field 'run.") + field + "' must be a string");
    return run.get<RunState>();
}

} // namespace stitching
