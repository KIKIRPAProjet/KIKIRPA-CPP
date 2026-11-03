// src/frontend/PipelineJson.cpp
#include "PipelineJson.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace FrontendNs {

    namespace fs = std::filesystem;

    ConfigNs::StepConfig makeStep(std::string name, std::vector<ConfigNs::StepParameter> parameters,
                                  std::vector<ConfigNs::DataSlot> inputs, std::vector<ConfigNs::DataSlot> outputs) {
        ConfigNs::StepConfig step;
        step.name = std::move(name);
        step.parameters = std::move(parameters);
        step.inputs = std::move(inputs);
        step.outputs = std::move(outputs);
        return step;
    }

    ConfigNs::StepParameter parameterFromText(const std::string& valueType, const std::string& name, const std::string& text) {
        const auto invalid = [&]() {
            return std::invalid_argument("valeur '" + text + "' invalide pour l'option '" + name + "' de type " + valueType);
        };

        if (valueType == "string") return {valueType, name, text};

        if (valueType == "int") {
            try {
                size_t usedCharacters = 0;
                const long long number = std::stoll(text, &usedCharacters);
                if (usedCharacters != text.size()) throw invalid();
                return {valueType, name, number};
            } catch (const std::logic_error&) {   // invalid_argument / out_of_range de stoll
                throw invalid();
            }
        }

        if (valueType == "float") {
            try {
                size_t usedCharacters = 0;
                const double number = std::stod(text, &usedCharacters);
                if (usedCharacters != text.size()) throw invalid();
                return {valueType, name, number};
            } catch (const std::logic_error&) {
                throw invalid();
            }
        }

        if (valueType == "bool") {
            std::string lowerCaseText = text;
            std::transform(lowerCaseText.begin(), lowerCaseText.end(), lowerCaseText.begin(), [](unsigned char character) { return std::tolower(character); });
            if (lowerCaseText == "true" || lowerCaseText == "1" || lowerCaseText == "yes" || lowerCaseText == "on") return {valueType, name, true};
            if (lowerCaseText == "false" || lowerCaseText == "0" || lowerCaseText == "no" || lowerCaseText == "off") return {valueType, name, false};
            throw invalid();
        }

        throw std::invalid_argument("type d'option non supporté '" + valueType + "' (string, int, float, bool)");
    }

    Json pipelineToJson(const ConfigNs::PipelineConfig& pipeline) {
        return {{"pipeline", pipeline}};
    }

    std::string pipelineToString(const ConfigNs::PipelineConfig& pipeline, int indent) {
        return pipelineToJson(pipeline).dump(indent);
    }

    void writePipelineJson(const ConfigNs::PipelineConfig& pipeline, const fs::path& file, int indent) {
        if (file.has_parent_path()) fs::create_directories(file.parent_path());

        std::ofstream output(file);
        output << pipelineToString(pipeline, indent) << '\n';
        if (!output) throw std::runtime_error("impossible d'écrire " + file.string());
    }

}
