#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct StitchingInput {
    std::string matcher = "sift";
    std::string rejector = "ransac";
    int level = 2;
    float ratio = 0.75f;
    float threshold = 3.0f;
};

// name : "stitching"
// options : matcher (string), rejector (string), level (int), ratio (float), threshold (float)
// pipelineOptions : { "type": "Tiles", "key": "tiles" }
// signature : Matches -> "matches", Alignments -> "alignments"
class StitchingStep : public Step {
public:
    explicit StitchingStep(const StitchingInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
