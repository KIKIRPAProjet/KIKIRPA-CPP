#include "Backend/core/PipelineBuilder.h"
#include "steps/ImportStep.h"
#include "steps/LightGlueStep.h"
#include <stdexcept>

namespace stitching {

Pipeline buildLightGluePipeline(const PipelineSettings& s) {
    Pipeline p;
    p.add(std::make_unique<steps::ImportStep>(steps::ImportInput{s.imageDir.string(), s.format}));
    p.add(std::make_unique<steps::LightGlueStep>(steps::LightGlueInput{s.modelPath, s.minMatches, s.overlap}));
    return p;
}

Pipeline buildPipeline(const PipelineSettings& s, Tuile&, Tuile&) {
    if (s.matcher != "lightglue")
        throw std::invalid_argument("buildPipeline currently supports only matcher='lightglue'");
    return buildLightGluePipeline(s);
}

} // namespace stitching
