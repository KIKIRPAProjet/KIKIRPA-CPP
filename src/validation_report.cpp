#include <pipeline/validation_report.hpp>

namespace pipeline {
Json Diagnostic::toJson() const {
    Json result = {{"code", code}, {"message", message}, {"details", details}};
    if (stepId) result["stepId"] = *stepId;
    return result;
}
Json ValidationReport::toJson() const {
    Json result = {{"valid", valid()}, {"errors", Json::array()},
                   {"warnings", Json::array()}, {"validatedSteps", validatedSteps},
                   {"executionOrder", nullptr}};
    for (const auto& e : errors) result["errors"].push_back(e.toJson());
    for (const auto& w : warnings) result["warnings"].push_back(w.toJson());
    if (executionOrder) result["executionOrder"] = *executionOrder;
    return result;
}
}
