#include "steps/StitchingStep.h"
#include "core/Config.h"

namespace stitching::steps {

StitchingStep::StitchingStep(const StitchingInput& input) {
    StepConfig cfg;
    cfg.name = "stitching";
    cfg.options = {{"string", "matcher", input.matcher},
                   {"string", "rejector", input.rejector},
                   {"int", "level", input.level},
                   {"float", "ratio", input.ratio},
                   {"float", "threshold", input.threshold}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{"Alignments", "alignments"}};
    config = cfg;
}

bool StitchingStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
