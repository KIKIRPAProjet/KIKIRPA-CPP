#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace pipeline {
using Json = nlohmann::json;
/** Diagnostic exploitable par une application : code stable, message et contexte. */
struct Diagnostic {
    std::string code, message;
    std::optional<std::string> stepId;
    Json details = Json::object();
    Json toJson() const;
};
/** Rapport de validation : executionOrder est fourni uniquement si le pipeline est entièrement valide. */
struct ValidationReport {
    std::vector<Diagnostic> errors, warnings;
    std::vector<std::string> validatedSteps;
    std::optional<std::vector<std::string>> executionOrder;
    bool valid() const { return errors.empty(); }
    Json toJson() const;
};
}
