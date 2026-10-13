#include <pipeline/pipeline_validator.hpp>
#include "diagnostic_utils.hpp"
#include <set>

namespace pipeline {
using detail::issue;
namespace {
bool nonemptyString(const Json& value) {
    return value.is_string() && !value.get<std::string>().empty();
}
}
std::map<std::string, Step> PipelineValidator::readSteps(const Json& document, ValidationReport& report) const {
    std::map<std::string, Step> steps;
    if (!document.is_object() || !document.contains("pipeline") || !document["pipeline"].is_object()) {
        report.errors.push_back(issue("INVALID_SCHEMA", "Expected a pipeline object.")); return steps;
    }
    const auto& p = document["pipeline"];
    for (const auto& key : {"name", "version"})
        if (!p.contains(key) || !nonemptyString(p[key]))
            report.errors.push_back(issue("INVALID_SCHEMA", "Pipeline metadata must be a nonempty string.", {}, {{"field", key}}));
    if (!p.contains("steps") || !p["steps"].is_array()) {
        report.errors.push_back(issue("INVALID_SCHEMA", "pipeline.steps must be an array.")); return steps;
    }
    if (p["steps"].empty() && !allowEmpty_)
        report.errors.push_back(issue("INVALID_SCHEMA", "Empty pipelines are disabled."));
    std::set<std::string> ids;
    for (std::size_t index = 0; index < p["steps"].size(); ++index) {
        const auto& item = p["steps"][index];
        auto schemaError = [&](const std::string& field) {
            report.errors.push_back(issue("INVALID_SCHEMA", "Invalid or missing step field.",
                item.is_object() && item.contains("id") && nonemptyString(item["id"])
                    ? std::optional<std::string>(item["id"].get<std::string>()) : std::nullopt,
                {{"index", index}, {"field", field}}));
        };
        if (!item.is_object()) { schemaError("step"); continue; }
        bool ok = true;
        for (const auto& key : {"id", "type"}) if (!item.contains(key) || !nonemptyString(item[key])) {
            schemaError(key); ok = false;
        }
        if (item.contains("id") && nonemptyString(item["id"]) && !ids.insert(item["id"].get<std::string>()).second)
            report.errors.push_back(issue("DUPLICATE_STEP_ID", "Step ID is declared more than once.", item["id"].get<std::string>()));
        if (!item.contains("config") || !item["config"].is_object()) { schemaError("config"); ok = false; }
        if (!item.contains("depends_on") || !item["depends_on"].is_array()) { schemaError("depends_on"); ok = false; }
        else for (const auto& dependency : item["depends_on"]) if (!nonemptyString(dependency)) {
            schemaError("depends_on[]"); ok = false;
        }
        if (!ok) continue;
        Step step{item["id"], item["type"], item["depends_on"].get<std::vector<std::string>>(), item["config"]};
        std::set<std::string> unique(step.dependencies.begin(), step.dependencies.end());
        if (unique.size() != step.dependencies.size()) report.warnings.push_back(issue("DUPLICATE_DEPENDENCY",
            "Repeated dependency links are treated as one edge.", step.id));
        steps.emplace(step.id, std::move(step));
    }
    return steps;
}
}
