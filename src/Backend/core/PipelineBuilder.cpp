#include "Backend/core/PipelineBuilder.h"
#include "steps/AbsolutePositioningStep.h"
#include "steps/ExportStep.h"
#include "steps/FusionStep.h"
#include "steps/ImportStep.h"
#include "steps/IntensityCorrectionStep.h"
#include "steps/SeamlineStep.h"
#include "steps/StitchingStep.h"
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace stitching {

namespace {

using StepCreator = std::unique_ptr<Step> (*)();

const std::map<std::string, StepCreator>& registry() {
    static const std::map<std::string, StepCreator> r = {
        {"import", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::ImportStep>();
         }},
        {"stitching", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::StitchingStep>();
         }},
        {"absolutePositioning", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::AbsolutePositioningStep>();
         }},
        {"intensityCorrection", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::IntensityCorrectionStep>();
         }},
        {"seamline", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::SeamlineStep>();
         }},
        {"fusion", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::FusionStep>();
         }},
        {"export", []() -> std::unique_ptr<Step> {
             return std::make_unique<steps::ExportStep>();
         }},
    };

    return r;
}

} // namespace

Pipeline buildPipeline(const Json& script) {
    if (!script.is_object() || !script.contains("pipeline") ||
        !script.at("pipeline").is_object() || !script.at("pipeline").contains("steps") ||
        !script.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("pipeline: expected an object containing a 'steps' array");

    Pipeline pipeline;
    std::map<std::string, std::string> available;

    for (const Json& stepJson : script.at("pipeline").at("steps")) {
        if (!stepJson.is_object() || !stepJson.contains("name") ||
            !stepJson.at("name").is_string())
            throw std::invalid_argument("pipeline step: expected a string 'name'");
        const std::string name = stepJson.at("name").get<std::string>();

        const auto it = registry().find(name);
        if (it == registry().end()) {
            throw std::invalid_argument("pipeline: unknown step '" + name + "'");
        }

        auto step = it->second();
        step->importConfig(stepJson);

        const StepConfig config = step->getConfig().get<StepConfig>();
        for (const PipelineOption& input : config.pipelineOptions) {
            const auto produced = available.find(input.key);
            if (produced == available.end())
                throw std::invalid_argument("step '" + name + "': input '" + input.key +
                                            "' (" + input.type + ") has no preceding producer");
            if (produced->second != input.type)
                throw std::invalid_argument("step '" + name + "': input '" + input.key +
                                            "' expects " + input.type + ", but producer supplies " +
                                            produced->second);
        }
        for (const PipelineOption& output : config.signature.pipelineOptions) {
            const auto existing = available.find(output.key);
            if (existing != available.end() && existing->second != output.type)
                throw std::invalid_argument("step '" + name + "': output '" + output.key +
                                            "' conflicts with existing type " + existing->second);
            available[output.key] = output.type;
        }
        pipeline.add(std::move(step));
    }

    if (pipeline.getSteps().empty())
        throw std::invalid_argument("pipeline: 'steps' must contain at least one step");
    return pipeline;
}

} // namespace stitching
