#include <pipeline/step_registry.hpp>
#include <stdexcept>
#include <utility>

namespace pipeline {
void StepRegistry::registerType(std::string type, StepRules rules) {
    if (type.empty() || rules_.count(type)) throw std::invalid_argument("Empty or duplicate registered type");
    for (const auto& rule : rules.dependencies)
        if (rule.types.empty()) throw std::invalid_argument("Dependency rule requires at least one type");
    rules_.emplace(std::move(type), std::move(rules));
}
const StepRules* StepRegistry::find(const std::string& type) const {
    auto it = rules_.find(type);
    return it == rules_.end() ? nullptr : &it->second;
}
}
