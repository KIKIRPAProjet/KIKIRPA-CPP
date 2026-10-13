#include <pipeline/pipeline_validator.hpp>
#include "ordered_json.hpp"
#include <pipeline/dependency_resolver.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace pipeline {
PipelineValidator::PipelineValidator(StepRegistry registry, bool allowEmpty)
    : registry_(std::move(registry)), allowEmpty_(allowEmpty) {}
ValidationReport PipelineValidator::validate(const Json& document) const {
    ValidationReport report;
    bool ordered = false;
    if (document.is_object() && document.contains("pipeline") && document["pipeline"].is_object() &&
        document["pipeline"].contains("steps") && document["pipeline"]["steps"].is_array()) {
        const auto& list = document["pipeline"]["steps"];
        ordered = list.empty() && !document["pipeline"].contains("name") && !document["pipeline"].contains("version");
        for (const auto& item : list)
            if (item.is_object() && (item.contains("name") || item.contains("options"))) ordered = true;
    }
    const auto normalized = ordered ? detail::normalizeOrdered(document, registry_, report) : document;
    auto steps = readSteps(normalized, report);
    validateRules(steps, report);
    auto resolution = DependencyResolver{}.resolve(steps);
    report.errors.insert(report.errors.end(), resolution.errors.begin(), resolution.errors.end());
    // Une étape en erreur et toutes les étapes qui en dépendent ne peuvent pas
    // être déclarées validées. Les branches indépendantes correctes conservent
    // leur statut local, sans autoriser l’exécution du pipeline globalement invalide.
    std::set<std::string> invalid;
    for (const auto& error : report.errors) {
        if (error.stepId) invalid.insert(*error.stepId);
        if (error.code == "CYCLIC_DEPENDENCY")
            for (const auto& id : error.details["cycle"]) invalid.insert(id.get<std::string>());
    }
    std::map<std::string, std::vector<std::string>> downstream;
    for (const auto& [id, step] : steps) for (const auto& dep : step.dependencies) downstream[dep].push_back(id);
    std::vector<std::string> queue(invalid.begin(), invalid.end());
    for (std::size_t i = 0; i < queue.size(); ++i)
        for (const auto& id : downstream[queue[i]]) if (invalid.insert(id).second) queue.push_back(id);
    for (const auto& [id, step] : steps) { (void)step; if (!invalid.count(id)) report.validatedSteps.push_back(id); }
    if (report.valid()) report.executionOrder = std::move(resolution.order);
    return report;
}
ValidationReport requireValid(const PipelineValidator& validator, const Json& document) {
    auto report = validator.validate(document);
    if (!report.valid()) throw std::runtime_error(report.toJson().dump(2));
    return report;
}
}
