#include "steps/IntensityCorrectionStep.h"
#include "core/Config.h"

namespace stitching::steps {

IntensityCorrectionStep::IntensityCorrectionStep(const IntensityCorrectionInput& input) {
    StepConfig cfg;
    cfg.name = "intensityCorrection";
    cfg.options = {{"bool", "enabled", input.enabled}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{"Tiles", "tiles"}};
    config = cfg;
}

bool IntensityCorrectionStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
