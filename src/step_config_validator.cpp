#include <pipeline/step_config_validator.hpp>
#include "diagnostic_utils.hpp"
#include <algorithm>

namespace pipeline {
using detail::issue;
bool matches(ConfigField::Type type, const Json& value) {
    switch (type) {
    case ConfigField::Type::String: return value.is_string();
    case ConfigField::Type::Number: return value.is_number();
    case ConfigField::Type::Integer: return value.is_number_integer();
    case ConfigField::Type::Boolean: return value.is_boolean();
    case ConfigField::Type::Object: return value.is_object();
    case ConfigField::Type::Array: return value.is_array();
    }
    return false;
}
std::vector<Diagnostic> StepConfigValidator::validate(const Step& step) const {
    std::vector<Diagnostic> errors;
    for (const auto& [name, rule] : fields) {
        auto it = step.config.find(name);
        if (it == step.config.end()) {
            if (rule.required) errors.push_back(issue("INVALID_CONFIGURATION",
                "Required configuration field is missing.", step.id, {{"field", name}}));
            continue;
        }
        bool ok = matches(rule.type, *it);
        if (ok && it->is_number()) {
            const auto number = it->get<double>();
            ok = (!rule.minimum || number >= *rule.minimum) &&
                 (!rule.maximum || number <= *rule.maximum);
        }
        if (!rule.allowed.empty())
            ok = ok && std::find(rule.allowed.begin(), rule.allowed.end(), *it) != rule.allowed.end();
        if (!ok) errors.push_back(issue("INVALID_CONFIGURATION",
            "Configuration field has an invalid type, range or value.", step.id,
            {{"field", name}, {"value", *it}}));
    }
    if (!allowUnknown) for (auto it = step.config.begin(); it != step.config.end(); ++it)
        if (!fields.count(it.key())) errors.push_back(issue("INVALID_CONFIGURATION",
            "Unknown configuration field.", step.id, {{"field", it.key()}}));
    return errors;
}
}
