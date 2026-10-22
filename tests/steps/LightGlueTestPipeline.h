#pragma once
#include "core/Pipeline.h"
#include "steps/ImportStep.h"
#include "steps/LightGlueStep.h"
#include <utility>

namespace stitching::tests {

// Parcours de validation indépendant du builder applicatif.
inline Pipeline buildLightGlueTestPipeline(
    const steps::ImportInput& input,
    const steps::LightGlueInput& lightGlue,
    steps::LightGlueStep::MatcherFactory factory = {}) {
    Pipeline pipeline;
    pipeline.add(std::make_unique<steps::ImportStep>(input));
    pipeline.add(std::make_unique<steps::LightGlueStep>(lightGlue, std::move(factory)));
    return pipeline;
}

} // namespace stitching::tests
