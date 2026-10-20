#include "core/PipelineJson.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

StepConfig makeStep(std::string name, std::vector<Option> options, std::vector<PipelineOption> inputs,
                    std::vector<PipelineOption> outputs) {
    StepConfig step;
    step.name = std::move(name);
    step.options = std::move(options);
    step.pipelineOptions = std::move(inputs);
    step.signature.pipelineOptions = std::move(outputs);
    return step;
}

Option optionFromText(const std::string& type, const std::string& key, const std::string& text) {
    const auto invalid = [&]() {
        return std::invalid_argument("valeur '" + text + "' invalide pour l'option '" + key + "' de type " + type);
    };

    if (type == "string") return {type, key, text};

    if (type == "int") {
        try {
            size_t used = 0;
            const long long v = std::stoll(text, &used);
            if (used != text.size()) throw invalid();
            return {type, key, v};
        } catch (const std::logic_error&) {   // invalid_argument / out_of_range de stoll
            throw invalid();
        }
    }

    if (type == "float") {
        try {
            size_t used = 0;
            const double v = std::stod(text, &used);
            if (used != text.size()) throw invalid();
            return {type, key, v};
        } catch (const std::logic_error&) {
            throw invalid();
        }
    }

    if (type == "bool") {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
        if (t == "true" || t == "1" || t == "yes" || t == "on") return {type, key, true};
        if (t == "false" || t == "0" || t == "no" || t == "off") return {type, key, false};
        throw invalid();
    }

    throw std::invalid_argument("type d'option non supporté '" + type + "' (string, int, float, bool)");
}

Json pipelineToJson(const PipelineConfig& pipeline) {
    return {{"pipeline", pipeline}};
}

std::string pipelineToString(const PipelineConfig& pipeline, int indent) {
    return pipelineToJson(pipeline).dump(indent);
}

void writePipelineJson(const PipelineConfig& pipeline, const fs::path& file, int indent) {
    if (file.has_parent_path()) fs::create_directories(file.parent_path());

    std::ofstream out(file);
    out << pipelineToString(pipeline, indent) << '\n';
    if (!out) throw std::runtime_error("impossible d'écrire " + file.string());
}

} // namespace stitching
