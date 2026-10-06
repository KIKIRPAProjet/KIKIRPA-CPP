#include "app/PipelineBuilder.h"
#include "algorithms/Factory.h"
#include "steps/AbsolutePositioningStep.h"
#include "steps/ExportStep.h"
#include "steps/FusionStep.h"
#include "steps/ImportStep.h"
#include "steps/IntensityCorrectionStep.h"
#include "steps/SeamlineStep.h"
#include "steps/StitchingStep.h"

namespace stitching {

Pipeline buildPipeline(const PipelineSettings& s, Tuile& a, Tuile& b) {
    PairContext ctx{s.direction, s.overlap};
    Pipeline p;
    p.add(std::make_unique<steps::ImportStep>(s.imageDir, std::regex(s.pattern)));
    Json matcherParams = {
        {"model_path", s.lightGlueModel.string()}
    };
    p.add(std::make_unique<steps::StitchingStep>(a, b, ctx,
            makeFeatureMatcher(s.matcher, matcherParams),
            makeOutlierRejector(s.rejector)));
    p.add(std::make_unique<steps::AbsolutePositioningStep>());
    p.add(std::make_unique<steps::IntensityCorrectionStep>());
    p.add(std::make_unique<steps::SeamlineStep>(a, b, ctx, makeSeamFinder(s.seamFinder)));
    p.add(std::make_unique<steps::FusionStep>());
    p.add(std::make_unique<steps::ExportStep>());
    return p;
}

} // namespace stitching
