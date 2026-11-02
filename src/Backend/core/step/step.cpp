// src/Backend/core/step/step.cpp
#include "core/step/step.h"

#include "core/config/pipeline_config.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace StepNs {

    namespace {
*
        // La valeur JSON correspond-elle au type déclaré de l'option ("int" n'accepte pas 2.5, "float" accepte 2)
        bool valueMatchesType(const Json& value, const std::string& valueType) {
            if (valueType == "string") return value.is_string();
            if (valueType == "int") return value.is_number_integer();
            if (valueType == "float") return value.is_number();
            if (valueType == "bool") return value.is_boolean();
            return false;
        }

        // Structure du JSON d'un step, avant toute conversion en StepConfig (messages précis plutôt qu'un type_error)
        void validateStepJson(const Json& config) {
            if (!config.is_object()) throw std::invalid_argument("step config must be an object");
            if (config.contains("name") && !config.at("name").is_string())
                throw std::invalid_argument("field 'name' must be a string");
            if (!config.contains("parameters")) return;
            if (!config.at("parameters").is_array()) throw std::invalid_argument("field 'parameters' must be an array");

            std::set<std::string> names;
            for (size_t index = 0; index < config.at("parameters").size(); ++index) {
                const Json& parameter = config.at("parameters").at(index);
                const std::string path = "parameters[" + std::to_string(index) + "]";
                if (!parameter.is_object()) throw std::invalid_argument(path + " must be an object");
                if (!parameter.contains("name") || !parameter.at("name").is_string() || parameter.at("name").get<std::string>().empty())
                    throw std::invalid_argument(path + ".name must be a non-empty string");
                if (!parameter.contains("valueType") || !parameter.at("valueType").is_string())
                    throw std::invalid_argument(path + ".valueType must be a string");
                if (!parameter.contains("value")) throw std::invalid_argument(path + ".value is required");
                if (!names.insert(parameter.at("name").get<std::string>()).second)
                    throw std::invalid_argument("duplicate parameter '" + parameter.at("name").get<std::string>() + "'");
            }
        }

    }

    Json StepClass::getParameter(const std::string& name) const {
        if (configJson.contains("parameters"))
            for (const auto& parameter : configJson.at("parameters"))
                if (parameter.value("name", "") == name) return parameter.at("value");
        throw std::out_of_range("Step '" + configJson.value("name", "") + "': no parameter '" + name + "'");
    }

    void StepClass::importConfig(const Json& newConfig) {
        validateStepJson(newConfig);
        if (!configJson.is_object()) {   // step sans config par défaut : rien à quoi comparer
            configJson = newConfig;
            return;
        }

        ConfigNs::StepConfig ownConfig = configJson.get<ConfigNs::StepConfig>();
        const ConfigNs::StepConfig importedConfig = newConfig.get<ConfigNs::StepConfig>();
        if (!importedConfig.name.empty() && importedConfig.name != ownConfig.name)
            throw std::invalid_argument("step '" + ownConfig.name + "' cannot import the config of step '" + importedConfig.name + "'");

        // Seules les valeurs sont reprises : les clés et les types des options, les entrées et les sorties
        // sont fixés par le step (une option absente du JSON garde sa valeur par défaut)
        for (const ConfigNs::StepParameter& importedParameter : importedConfig.parameters) {
            const auto ownParameter = std::find_if(ownConfig.parameters.begin(), ownConfig.parameters.end(),
                                        [&](const ConfigNs::StepParameter& candidate) { return candidate.name == importedParameter.name; });
            if (ownParameter == ownConfig.parameters.end())
                throw std::invalid_argument("step '" + ownConfig.name + "': unknown parameter '" + importedParameter.name + "'");
            if (importedParameter.valueType != ownParameter->valueType)
                throw std::invalid_argument("step '" + ownConfig.name + "': parameter '" + importedParameter.name + "' has type '" + ownParameter->valueType +
                                            "', got '" + importedParameter.valueType + "'");
            if (!valueMatchesType(importedParameter.value, ownParameter->valueType))
                throw std::invalid_argument("step '" + ownConfig.name + "': parameter '" + importedParameter.name + "' expects a value of type '" +
                                            ownParameter->valueType + "', got " + importedParameter.value.dump());
            ownParameter->value = importedParameter.value;
        }
        configJson = ownConfig;
    }

}
