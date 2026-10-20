#include "core/Step.h"
#include "core/Config.h"
#include <algorithm>
#include <stdexcept>
#include <set>

namespace stitching {

namespace {

std::string jsonTypeName(const Json& value) {
    if (value.is_null()) return "null";
    if (value.is_boolean()) return "boolean";
    if (value.is_number_integer()) return "integer";
    if (value.is_number_float()) return "number";
    if (value.is_string()) return "string";
    if (value.is_array()) return "array";
    if (value.is_object()) return "object";
    return "unknown";
}

bool hasSameJsonType(const Json& value, const Json& expected) {
    if (expected.is_number()) return value.is_number();
    return value.type() == expected.type();
}

void validateStepConfig(const Json& cfg) {
    if (!cfg.is_object()) throw std::invalid_argument("step config must be an object");

    if (cfg.contains("name") && !cfg.at("name").is_string())
        throw std::invalid_argument("step field 'name' must be a string");

    if (!cfg.contains("options")) return;
    if (!cfg.at("options").is_array())
        throw std::invalid_argument("step field 'options' must be an array");

    std::set<std::string> keys;
    for (size_t i = 0; i < cfg.at("options").size(); ++i) {
        const Json& option = cfg.at("options").at(i);
        const std::string path = "options[" + std::to_string(i) + "]";
        if (!option.is_object())
            throw std::invalid_argument("step field '" + path + "' must be an object");
        if (!option.contains("key") || !option.at("key").is_string() || option.at("key").get<std::string>().empty())
            throw std::invalid_argument("step field '" + path + ".key' must be a non-empty string");
        if (!option.contains("type") || !option.at("type").is_string() || option.at("type").get<std::string>().empty())
            throw std::invalid_argument("step field '" + path + ".type' must be a non-empty string");
        if (!option.contains("value"))
            throw std::invalid_argument("step field '" + path + ".value' is required");

        const std::string key = option.at("key").get<std::string>();
        if (!keys.insert(key).second)
            throw std::invalid_argument("step has duplicate option '" + key + "'");
    }
}

} //

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options"))
        for (const auto& o : config.at("options"))
            if (o.value("key", "") == key) return o.at("value");
    const std::string name = config.is_object() ? config.value("name", "?") : "?";
    throw std::out_of_range("step '" + name + "': missing option '" + key + "'");
}

// Mettre à jour (importer) la config. du step pour remplacer la config. actuelle (appelé depuis buildPipeline dans PipelineBuilder.cpp)
void Step::importConfig(const Json& cfg) {
    validateStepConfig(cfg);
    if (!config.is_object()) {   // step sans config par défaut -> on remplace la config. comme celle reçue et basta.
        config = cfg;
        return;
    }

    StepConfig own = config.get<StepConfig>();
    const StepConfig in = cfg.get<StepConfig>();
    if (!in.name.empty() && in.name != own.name) // Validation du nom
        throw std::invalid_argument("step '" + own.name + "': cannot import config of step '" + in.name + "'");

    for (const Option& o : in.options) { // Maj sécurisée des options
        auto it = std::find_if(own.options.begin(), own.options.end(),
                               [&](const Option& d) { return d.key == o.key; });
        if (it == own.options.end())
            throw std::invalid_argument("step '" + own.name + "': unknown option '" + o.key + "'");
        if (o.type != it->type)
            throw std::invalid_argument("step '" + own.name + "': option '" + o.key + "' expects type '" +
        it->value = o.value;
    }
    config = own; // Écrase l'ancienne config par la nouvelle
}

} // namespace stitching
