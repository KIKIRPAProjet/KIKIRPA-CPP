#include "steps/FusionStep.h"
#include "core/Config.h"

namespace stitching::steps {

FusionStep::FusionStep(const FusionInput& input) {
    StepConfig cfg;
    cfg.name = "fusion";
    cfg.options = {{"string", "blending", input.blending}, {"int", "bands", input.bands}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}, {"Positions", "positions"}, {"Seams", "seams"}};
    cfg.signature.pipelineOptions = {{"Mosaic", "mosaic"}};
    config = cfg;
}

bool FusionStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
