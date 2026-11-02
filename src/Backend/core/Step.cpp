// Step.cpp
#include "core/Step.h"
#include "core/Config.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace stitching {

namespace {

// La valeur JSON correspond-elle au type déclaré de l'option ("int" n'accepte pas 2.5, "float" accepte 2)
bool valueMatchesType(const Json& value, const std::string& type) {
    if (type == "string") return value.is_string();
    if (type == "int") return value.is_number_integer();
    if (type == "float") return value.is_number();
    if (type == "bool") return value.is_boolean();
    return false;
}

// Structure du JSON d'un step, avant toute conversion en StepConfig (messages précis plutôt qu'un type_error)
void validateStepJson(const Json& cfg) {
    if (!cfg.is_object()) throw std::invalid_argument("step config must be an object");
    if (cfg.contains("name") && !cfg.at("name").is_string())
        throw std::invalid_argument("field 'name' must be a string");
    if (!cfg.contains("options")) return;
    if (!cfg.at("options").is_array()) throw std::invalid_argument("field 'options' must be an array");

    std::set<std::string> keys;
    for (size_t i = 0; i < cfg.at("options").size(); ++i) {
        const Json& o = cfg.at("options").at(i);
        const std::string path = "options[" + std::to_string(i) + "]";
        if (!o.is_object()) throw std::invalid_argument(path + " must be an object");
        if (!o.contains("key") || !o.at("key").is_string() || o.at("key").get<std::string>().empty())
            throw std::invalid_argument(path + ".key must be a non-empty string");
        if (!o.contains("type") || !o.at("type").is_string())
            throw std::invalid_argument(path + ".type must be a string");
        if (!o.contains("value")) throw std::invalid_argument(path + ".value is required");
        if (!keys.insert(o.at("key").get<std::string>()).second)
            throw std::invalid_argument("duplicate option '" + o.at("key").get<std::string>() + "'");
    }
}

} // namespace

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options") && config.at("options").is_array())
        for (const auto& opt : config.at("options"))
            if (opt.is_object() && opt.value("key", "") == key)
                return opt.at("value");
    const std::string name = config.is_object() && config.contains("name") && config.at("name").is_string()
                                 ? config.at("name").get<std::string>() : "?";
    throw std::out_of_range("Step '" + name + "': no option '" + key + "'");
}

void Step::importConfig(const Json& cfg) {
    validateStepJson(cfg);
    if (!config.is_object()) {   // step sans config par défaut : rien à quoi comparer
        config = cfg;
        return;
    }

    StepConfig own = config.get<StepConfig>();
    const StepConfig in = cfg.get<StepConfig>();
    if (!in.name.empty() && in.name != own.name)
        throw std::invalid_argument("step '" + own.name + "' cannot import the config of step '" + in.name + "'");

    // Seules les valeurs sont reprises : les clés et les types des options, les entrées et les sorties
    // sont fixés par le step (une option absente du JSON garde sa valeur par défaut)
    for (const Option& o : in.options) {
        const auto it = std::find_if(own.options.begin(), own.options.end(),
                                     [&](const Option& d) { return d.key == o.key; });
        if (it == own.options.end())
            throw std::invalid_argument("step '" + own.name + "': unknown option '" + o.key + "'");
        if (o.type != it->type)
            throw std::invalid_argument("step '" + own.name + "': option '" + o.key + "' has type '" + it->type +
                                        "', got '" + o.type + "'");
        if (!valueMatchesType(o.value, it->type))
            throw std::invalid_argument("step '" + own.name + "': option '" + o.key + "' expects a value of type '" +
                                        it->type + "', got " + o.value.dump());
        it->value = o.value;
    }
    config = own;
}

} // namespace stitching
