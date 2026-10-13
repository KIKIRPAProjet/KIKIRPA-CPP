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

Pipeline buildPipeline(const Json& script) {
    Pipeline p;
    std::map<std::string, std::string> produced;   // clé → type des données déjà produites

    for (const Json& stepJson : script.at("pipeline").at("steps")) {
        const std::string name = stepJson.at("name").get<std::string>();
        const auto it = registry().find(name);
        if (it == registry().end()) throw std::invalid_argument("unknown step '" + name + "'");

        auto step = it->second();
        step->importConfig(stepJson);

        // Enchaînement : chaque entrée doit être la sortie d'un step précédent
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
    return p;
}

} // namespace stitching
