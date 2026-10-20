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
    Pipeline pipeline;

    for (const Json& stepJson : script.at("pipeline").at("steps")) {
        const std::string name = stepJson.at("name").get<std::string>();

        const auto it = registry().find(name);
        if (it == registry().end()) {
            throw std::invalid_argument("unknown step '" + name + "'");
        }

        auto step = it->second();
        step->importConfig(stepJson);

        pipeline.add(std::move(step));
    }

    return pipeline;
}

} // namespace stitching