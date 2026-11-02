#pragma once
#include "core/Step.h"

namespace stitching::steps {

// Entrée du step = ses options
struct AbsolutePositioningInput {
    int referenceTile = 0;
};

// name : "absolutePositioning"
// options : referenceTile (int)
// pipelineOptions : Tiles -> "tiles", Alignments -> "alignments"
// signature : { "type": "Positions", "key": "positions" }
class AbsolutePositioningStep : public Step {
public:
    explicit AbsolutePositioningStep(const AbsolutePositioningInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
