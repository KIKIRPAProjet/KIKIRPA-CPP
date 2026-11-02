#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct SeamlineInput {
    std::string seamFinder = "none";
    int level = 1;
};

// name : "seamline"
// options : seamFinder (string), actuellement "none"
// pipelineOptions : Tiles -> "tiles", Positions -> "positions"
// signature : { "type": "Territories", "key": "territories" }
class SeamlineStep : public Step {
public:
    explicit SeamlineStep(const SeamlineInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
