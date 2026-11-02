#pragma once
#include "core/Step.h"

namespace stitching::steps {

// Entrée du step = ses options
struct IntensityCorrectionInput {
    bool enabled = true;
};

// name : "intensityCorrection"
// options : enabled (bool)
// pipelineOptions : Tiles -> "tiles"
// signature : LightCorrections -> "corrections"
class IntensityCorrectionStep : public Step {
public:
    explicit IntensityCorrectionStep(const IntensityCorrectionInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
