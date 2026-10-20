#include "Backend/core/PipelineBuilder.h"
#include "core/Config.h"
#include "steps/AbsolutePositioningStep.h"
#include "steps/ExportStep.h"
#include "steps/FusionStep.h"
#include "steps/ImportStep.h"
#include "steps/IntensityCorrectionStep.h"
#include "steps/SeamlineStep.h"
#include "steps/StitchingStep.h"
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>

namespace stitching {

namespace {

using StepCreator = std::function<std::unique_ptr<Step>()>;

// Nom du step ("name" dans le JSON) → création du step avec sa config par défaut.
// Une ligne par step.
// Registre interne qui lie les noms JSON aux classes C++ correspondantes. Chaque step est créé avec sa config par défaut (ex. "import" → ImportStep avec ImportInput par défaut).
const std::map<std::string, StepCreator>& registry() {
    static const std::map<std::string, StepCreator> r = {
        {"import", [] { return std::make_unique<steps::ImportStep>(); }},
        {"stitching", [] { return std::make_unique<steps::StitchingStep>(); }},
        {"absolutePositioning", [] { return std::make_unique<steps::AbsolutePositioningStep>(); }},
        {"intensityCorrection", [] { return std::make_unique<steps::IntensityCorrectionStep>(); }},
        {"seamline", [] { return std::make_unique<steps::SeamlineStep>(); }},
        {"fusion", [] { return std::make_unique<steps::FusionStep>(); }},
        {"export", [] { return std::make_unique<steps::ExportStep>(); }},
    };
    return r;
}

} // namespace

const std::vector<std::string>& defaultStepOrder() {
    static const std::vector<std::string> order = {"import", "stitching", "absolutePositioning", "intensityCorrection",
                                                   "seamline", "fusion", "export"};
    return order;
}

std::unique_ptr<Step> createStep(const std::string& name) {
    const auto it = registry().find(name);
    if (it == registry().end()) throw std::invalid_argument("unknown step '" + name + "'");
    return it->second();
}

Pipeline buildPipeline(const Json& script) {
    if (!script.is_object())
        throw std::invalid_argument("pipeline script must be a JSON object");
    if (!script.contains("pipeline") || !script.at("pipeline").is_object())
        throw std::invalid_argument("field 'pipeline' must be an object");
    if (!script.at("pipeline").contains("steps") || !script.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("field 'pipeline.steps' must be an array");

    Pipeline p;
    std::map<std::string, std::string> produced;   // clé → type des données déjà produites

    const Json& stepConfigs = script.at("pipeline").at("steps");
    for (size_t i = 0; i < stepConfigs.size(); ++i) {
        const Json& stepJson = stepConfigs.at(i);
        const std::string path = "pipeline.steps[" + std::to_string(i) + "]";
        if (!stepJson.is_object())
            throw std::invalid_argument(path + " must be an object");
        if (!stepJson.contains("name") || !stepJson.at("name").is_string() ||
            stepJson.at("name").get<std::string>().empty())
            throw std::invalid_argument(path + ".name must be a non-empty string");
        const std::string name = stepJson.at("name").get<std::string>();
        // On cherche le nom du step (ex. "import", "stitching", etc.) dans le registre ; s'il n'y est pas, createStep lève une exception
        std::unique_ptr<Step> step;
        try {
            step = createStep(name);
            step->importConfig(stepJson);
        } catch (const std::exception& e) {
            throw std::invalid_argument(path + ": " + e.what());
        }

        // Enchaînement : chaque entrée doit être la sortie d'un step précédent
        // On vérifie donc qu'une étape n'est pas manquante ou qu'une a été déplacée avant une autre (on ne peut pas faire stitching avant import)
        const StepConfig cfg = step->getConfig().get<StepConfig>();
        for (const PipelineOption& in : cfg.pipelineOptions) {
            const auto src = produced.find(in.key);
            if (src == produced.end())
                throw std::invalid_argument("step '" + name + "': input '" + in.key +
                                            "' is not produced by a previous step");
            if (src->second != in.type)
                throw std::invalid_argument("step '" + name + "': input '" + in.key + "' expects type '" +
                                            in.type + "', got '" + src->second + "'");
        }
        for (const PipelineOption& out : cfg.signature.pipelineOptions) produced[out.key] = out.type;

        p.add(std::move(step));
    }
    if (script.contains("run")) p.setRunState(script.at("run").get<RunState>());   // optionnel : état d'un run précédent
    return p;
}

} // namespace stitching
