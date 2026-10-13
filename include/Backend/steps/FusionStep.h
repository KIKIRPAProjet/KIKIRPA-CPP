#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct FusionInput {
    std::string blending = "multiband";
    int bands = 5;
};

// name : "fusion"
// options : blending (string), bands (int)
// pipelineOptions : { "type": "Tiles", "key": "tiles" }, { "type": "Positions", "key": "positions" }, { "type": "Seams", "key": "seams" }
// signature : { "type": "Mosaic", "key": "mosaic" }
class FusionStep : public Step {
public:
    explicit FusionStep(const FusionInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
