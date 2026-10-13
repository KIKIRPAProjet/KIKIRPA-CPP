#include <pipeline/pipeline_validator.hpp>
#include "diagnostic_utils.hpp"
#include <algorithm>
#include <exception>

namespace pipeline {
using detail::issue;
void PipelineValidator::validateRules(const std::map<std::string, Step>& steps, ValidationReport& report) const {
    for (const auto& [id, step] : steps) {
        const auto* rules = registry_.find(step.type);
        if (!rules) { report.errors.push_back(issue("UNKNOWN_STEP_TYPE", "Step type is not registered.", id, {{"type", step.type}})); continue; }
        auto configErrors = rules->configuration.validate(step);
        report.errors.insert(report.errors.end(), configErrors.begin(), configErrors.end());
        if (rules->customValidator) {
            try {
                auto errors = rules->customValidator(step);
                for (auto& error : errors) { error.stepId = id; report.errors.push_back(std::move(error)); }
            } catch (const std::exception& error) {
                report.errors.push_back(issue("INVALID_CONFIGURATION", "Custom validator failed.", id, {{"reason", error.what()}}));
            } catch (...) {
                report.errors.push_back(issue("INVALID_CONFIGURATION", "Custom validator failed with a non-standard exception.", id));
            }
        }
        for (const auto& rule : rules->dependencies) {
            if (rule.optional) continue;
            bool found = false;
            for (const auto& dependency : step.dependencies) {
                auto it = steps.find(dependency);
                if (it == steps.end() || dependency == id) continue;
                found = found || ((!rule.id || dependency == *rule.id) &&
                    std::find(rule.types.begin(), rule.types.end(), it->second.type) != rule.types.end());
            }
            if (!found) {
                Json details = {{"acceptedTypes", rule.types}, {"requiresDirectLink", true}};
                if (rule.id) details["requiredId"] = *rule.id;
                report.errors.push_back(issue("MISSING_DEPENDENCY", "Required explicit dependency is missing.", id, details));
            }
        }
        if (rules->acceptedInputs.empty()) continue;
        for (const auto& dependency : step.dependencies) {
            auto it = steps.find(dependency);
            if (it == steps.end()) continue;
            auto producer = registry_.find(it->second.type);
            if (!producer) continue;
            bool compatible = false;
            for (const auto& output : producer->outputs)
                compatible = compatible || std::find(rules->acceptedInputs.begin(), rules->acceptedInputs.end(), output) != rules->acceptedInputs.end();
            if (!compatible) report.errors.push_back(issue("INCOMPATIBLE_STEP_OUTPUT", "Dependency output contract is incompatible.", id,
                {{"dependencyId", dependency}, {"outputs", producer->outputs}, {"acceptedInputs", rules->acceptedInputs}}));
        }
    }
}
}
