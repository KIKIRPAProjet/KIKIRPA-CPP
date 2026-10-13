#include "steps/SeamlineStep.h"
#include "core/Config.h"

namespace stitching::steps {

SeamlineStep::SeamlineStep(const SeamlineInput& input) {
    StepConfig cfg;
    cfg.name = "seamline";
    cfg.options = {{"string", "seamFinder", input.seamFinder}, {"int", "level", input.level}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}, {"Positions", "positions"}};
    cfg.signature.pipelineOptions = {{"Seams", "seams"}};
    config = cfg;
}

bool SeamlineStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
