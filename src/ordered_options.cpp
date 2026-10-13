#include "ordered_json.hpp"
#include "diagnostic_utils.hpp"
#include <set>

namespace pipeline::detail {
Json readOptions(const Json& options, const std::string& id, ValidationReport& report) {
    Json config = Json::object();
    if (!options.is_array()) {
        report.errors.push_back(issue("INVALID_SCHEMA", "options doit être un tableau.", id));
        return config;
    }
    std::set<std::string> keys;
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto& option = options[i];
        if (!option.is_object() || !option.contains("key") || !option["key"].is_string() ||
            option["key"].get<std::string>().empty() || !option.contains("type") ||
            !option["type"].is_string() || !option.contains("value")) {
            report.errors.push_back(issue("INVALID_SCHEMA", "Option attendue : type, key et value.", id, {{"optionIndex", i}}));
            continue;
        }
        const auto key = option["key"].get<std::string>();
        if (!keys.insert(key).second) {
            report.errors.push_back(issue("INVALID_CONFIGURATION", "Clé d’option répétée.", id, {{"field", key}}));
            continue;
        }
        const auto type = option["type"].get<std::string>();
        const auto& value = option["value"];
        const bool correct = (type == "string" && value.is_string()) ||
            (type == "int" && value.is_number_integer()) ||
            (type == "float" && value.is_number()) || (type == "bool" && value.is_boolean());
        if (!correct) report.errors.push_back(issue("INVALID_CONFIGURATION",
            "La valeur ne correspond pas au type déclaré, ou le type est inconnu.", id,
            {{"field", key}, {"declaredType", type}, {"value", value}}));
        config[key] = value;
    }
    return config;
}
}
