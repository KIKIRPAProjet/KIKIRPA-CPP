#include "steps/ExportStep.h"
#include "core/Config.h"

namespace stitching::steps {

ExportStep::ExportStep(const ExportInput& input) {
    StepConfig cfg;
    cfg.name = "export";
    cfg.options = {{"string", "path", input.path},
                   {"string", "format", input.format},
                   {"int", "bits", input.bits},
                   {"int", "blockSize", input.blockSize},
                   {"bool", "keepIcc", input.keepIcc},
                   {"bool", "pyramid", input.pyramid}};
    cfg.pipelineOptions = {{"Mosaic", "mosaic"}};
    config = cfg;
}

bool ExportStep::execute() {
    // TODO
    return true;
}

} // namespace stitching::steps
