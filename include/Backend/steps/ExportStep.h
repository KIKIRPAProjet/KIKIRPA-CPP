#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct ExportInput {
    std::string path = "output/mosaic.tif";
    int bits = 16;  
};

// name : "export"
// options : path (string), bits (int, 8 or 16)
// pipelineOptions : { "type": "Mosaic", "key": "mosaic" }
// signature : aucune
class ExportStep : public Step {
public:
    explicit ExportStep(const ExportInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
