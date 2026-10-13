#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct SeamlineInput {
    std::string seamFinder = "graphcut";
    int level = 1;
};

// name : "seamline"
// options : seamFinder (string), level (int)
// pipelineOptions : { "type": "Tiles", "key": "tiles" }, { "type": "Positions", "key": "positions" }
// signature : { "type": "Seams", "key": "seams" }
class SeamlineStep : public Step {
public:
    explicit SeamlineStep(const SeamlineInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
