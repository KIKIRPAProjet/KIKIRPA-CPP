#include "core/Step.h"
#include "core/Config.h"
#include <algorithm>
#include <cmath>
#include <regex>
#include <stdexcept>

namespace stitching {

namespace {

bool matchesType(const Json& value, const std::string& type) {
    if (type == "string") return value.is_string();
    if (type == "int") return value.is_number_integer();
    if (type == "float") return value.is_number();
    if (type == "bool") return value.is_boolean();
    return false;
}

void validateOption(const Option& option, const std::string& stepName) {
    if (option.key.empty())
        throw std::invalid_argument("step '" + stepName + "': option key cannot be empty");
    if (option.type != "string" && option.type != "int" &&
        option.type != "float" && option.type != "bool")
        throw std::invalid_argument("step '" + stepName + "', option '" + option.key +
                                    "': unsupported type '" + option.type + "'");
    if (!matchesType(option.value, option.type))
        throw std::invalid_argument("step '" + stepName + "', option '" + option.key +
                                    "': expected " + option.type + " value");
    if (option.type == "float" && !std::isfinite(option.value.get<double>()))
        throw std::invalid_argument("step '" + stepName + "', option '" + option.key +
                                    "': value must be finite");
}

void validateStepOptions(const StepConfig& config) {
    const auto value = [&](const std::string& key) -> const Json& {
        const auto it = std::find_if(config.options.begin(), config.options.end(),
                                     [&](const Option& option) { return option.key == key; });
        if (it == config.options.end())
            throw std::invalid_argument("step '" + config.name + "': missing default option '" + key + "'");
        return it->value;
    };
    const auto invalid = [&](const std::string& key, const std::string& reason) {
        throw std::invalid_argument("step '" + config.name + "', option '" + key + "': " + reason);
    };

    if (config.name == "import") {
        if (value("path").get<std::string>().empty()) invalid("path", "cannot be empty");
        const std::string format = value("format").get<std::string>();
        try {
            if (std::regex(format).mark_count() < 2)
                invalid("format", "regex must contain capture groups for column and row");
        } catch (const std::regex_error& e) {
            invalid("format", std::string("invalid regex: ") + e.what());
        }
    } else if (config.name == "stitching") {
        const std::string matcher = value("matcher").get<std::string>();
        if (matcher != "sift" && matcher != "orb") invalid("matcher", "must be 'sift' or 'orb'");
        if (value("rejector").get<std::string>() != "ransac") invalid("rejector", "must be 'ransac'");
        const int level = value("level").get<int>();
        if (level < 0 || level > 5) invalid("level", "must be between 0 and 5");
        const double ratio = value("ratio").get<double>();
        if (!(ratio > 0.0 && ratio < 1.0)) invalid("ratio", "must be in (0, 1)");
        if (value("threshold").get<double>() <= 0.0) invalid("threshold", "must be positive");
    } else if (config.name == "absolutePositioning") {
        if (value("referenceTile").get<int>() < 0)
            invalid("referenceTile", "must be non-negative");
    } else if (config.name == "seamline") {
        if (value("seamFinder").get<std::string>() != "none")
            invalid("seamFinder", "currently only 'none' is implemented");
    } else if (config.name == "fusion") {
        if (value("blending").get<std::string>() != "average")
            invalid("blending", "currently only 'average' is implemented");
    } else if (config.name == "export") {
        if (value("path").get<std::string>().empty()) invalid("path", "cannot be empty");
        const int bits = value("bits").get<int>();
        if (bits != 8 && bits != 16) invalid("bits", "must be 8 or 16");
    }
}

} // namespace

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options"))
        for (const auto& o : config.at("options"))
            if (o.value("key", "") == key) return o.at("value");
    const std::string name = config.is_object() ? config.value("name", "?") : "?";
    throw std::out_of_range("step '" + name + "': missing option '" + key + "'");
}

void Step::importConfig(const Json& cfg) {
    if (!config.is_object()) {   // step sans config par défaut
        config = cfg;
        return;
    }

    StepConfig own = config.get<StepConfig>();
    if (!cfg.is_object() || !cfg.contains("name") || !cfg.at("name").is_string())
        throw std::invalid_argument("step '" + own.name + "': expected a string 'name'");
    for (auto it = cfg.begin(); it != cfg.end(); ++it) {
        if (it.key() != "name" && it.key() != "options" &&
            it.key() != "pipelineOptions" && it.key() != "signature")
            throw std::invalid_argument("step '" + own.name + "': unknown field '" + it.key() + "'");
    }
    if (cfg.contains("options") && !cfg.at("options").is_array())
        throw std::invalid_argument("step '" + own.name + "': 'options' must be an array");

    StepConfig in;
    try {
        in = cfg.get<StepConfig>();
    } catch (const Json::exception& e) {
        throw std::invalid_argument("step '" + own.name + "': invalid configuration: " + e.what());
    }
    if (!in.name.empty() && in.name != own.name)
        throw std::invalid_argument("step '" + own.name + "': cannot import config of step '" + in.name + "'");
    if (cfg.contains("pipelineOptions") &&
        Json(in.pipelineOptions) != Json(own.pipelineOptions))
        throw std::invalid_argument("step '" + own.name + "': input pipelineOptions do not match the declared contract");
    if (cfg.contains("signature") &&
        Json(in.signature.pipelineOptions) != Json(own.signature.pipelineOptions))
        throw std::invalid_argument("step '" + own.name + "': output signature does not match the declared contract");

    std::vector<std::string> seen;
    for (const Option& o : in.options) {
        validateOption(o, own.name);
        if (std::find(seen.begin(), seen.end(), o.key) != seen.end())
            throw std::invalid_argument("step '" + own.name + "': duplicate option '" + o.key + "'");
        seen.push_back(o.key);
        auto it = std::find_if(own.options.begin(), own.options.end(),
                               [&](const Option& d) { return d.key == o.key; });
        if (it == own.options.end())
            throw std::invalid_argument("step '" + own.name + "': unknown option '" + o.key + "'");
        if (o.type != it->type)
            throw std::invalid_argument("step '" + own.name + "', option '" + o.key +
                                        "': expected type '" + it->type + "', got '" + o.type + "'");
        it->value = o.value;
    }
    for (const Option& option : own.options) validateOption(option, own.name);
    validateStepOptions(own);
    config = own;
}

} // namespace stitching
