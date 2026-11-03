// PipelineBuilder.cpp
#include "core/PipelineBuilder.h"
#include "core/Config.h"
#include "steps/GraphCutStep.h"
#include "steps/ImportStep.h"
#include "steps/PhaseCorrelationStep.h"
#include "steps/FusionStep.h"
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>

namespace stitching {

namespace {

using StepCreator = std::function<std::unique_ptr<Step>()>;

// Une ligne par step. Le nom doit être celui que le step écrit dans sa config (cfg.name).
const std::map<std::string, StepCreator>& registry() {
    static const std::map<std::string, StepCreator> r = {
        {"import", [] { return std::make_unique<steps::ImportStep>(); }},
        {"phase-correlation", [] { return std::make_unique<steps::PhaseCorrelationStep>(); }},
        {"fusion", [] { return std::make_unique<steps::FusionStep>(); }},
        {"graphcut", [] { return std::make_unique<steps::GraphCutStep>(); }},
    };
    return r;
}

} // namespace

std::vector<std::string> registeredSteps() {
    std::vector<std::string> names;
    for (const auto& entry : registry()) names.push_back(entry.first);
    return names;
}

std::unique_ptr<Step> createStep(const std::string& name) {
    const auto it = registry().find(name);
    if (it == registry().end()) throw std::invalid_argument("unknown step '" + name + "'");
    return it->second();
}

Pipeline buildPipeline(const Json& script) {
    if (!script.is_object()) throw std::invalid_argument("pipeline script must be a JSON object");
    if (!script.contains("pipeline") || !script.at("pipeline").is_object())
        throw std::invalid_argument("field 'pipeline' must be an object");
    if (!script.at("pipeline").contains("steps") || !script.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("field 'pipeline.steps' must be an array");

    Pipeline p;
    std::map<std::string, std::string> produced;   // clé -> type des données produites par les steps précédents
    const Json& stepsJson = script.at("pipeline").at("steps");
    for (size_t i = 0; i < stepsJson.size(); ++i) {
        const Json& stepJson = stepsJson.at(i);
        const std::string path = "pipeline.steps[" + std::to_string(i) + "]";
        if (!stepJson.is_object() || !stepJson.contains("name") || !stepJson.at("name").is_string())
            throw std::invalid_argument(path + ": a step must be an object with a string 'name'");
        const std::string name = stepJson.at("name").get<std::string>();

        std::unique_ptr<Step> step;
        try {
            step = createStep(name);
            step->importConfig(stepJson);
        } catch (const std::exception& e) {
            throw std::invalid_argument(path + ": " + e.what());
        }

        // Enchaînement : chaque entrée doit avoir été produite plus tôt, avec le même type
        const StepConfig cfg = step->getConfig().get<StepConfig>();
        for (const PipelineOption& in : cfg.pipelineOptions) {
            const auto src = produced.find(in.key);
            if (src == produced.end())
                throw std::invalid_argument(path + " ('" + name + "'): input '" + in.key +
                                            "' is not produced by a previous step");
            if (src->second != in.type)
                throw std::invalid_argument(path + " ('" + name + "'): input '" + in.key + "' expects type '" +
                                            in.type + "', got '" + src->second + "'");
        }
        for (const PipelineOption& out : cfg.signature.pipelineOptions) produced[out.key] = out.type;

        p.add(std::move(step));
    }
    if (script.contains("run")) p.setRunState(parseRunState(script.at("run")));   // état d'un run précédent
    return p;
}

Pipeline buildPipelineFromFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    Json script;
    try {
        script = Json::parse(in);
    } catch (const Json::parse_error& e) {
        throw std::runtime_error("invalid JSON in " + file.string() + ": " + e.what());
    }
    try {
        return buildPipeline(script);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(file.string() + ": " + e.what());
    }
}

} // namespace stitching

