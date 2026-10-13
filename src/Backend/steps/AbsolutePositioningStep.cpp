#include "steps/AbsolutePositioningStep.h"
#include "core/Config.h"

namespace stitching::steps {

AbsolutePositioningStep::AbsolutePositioningStep(const AbsolutePositioningInput& input) {
    StepConfig cfg;
    cfg.name = "absolutePositioning";
    cfg.options = {{"int", "referenceTile", input.referenceTile}};
    cfg.pipelineOptions = {{"Alignments", "alignments"}};
    cfg.signature.pipelineOptions = {{"Positions", "positions"}};
    config = cfg;
}

bool AbsolutePositioningStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
