#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct FusionInput {
    std::string blending = "average";
    int bands = 5;
};

// name : "fusion"
// options : blending (string), currently "average"
// pipelineOptions : Tiles, Positions, LightCorrections, Territories
// signature : { "type": "Mosaic", "key": "mosaic" }
class FusionStep : public Step {
public:
    explicit FusionStep(const FusionInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
