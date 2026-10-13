#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct ExportInput {
    std::string path = "output/mosaic.tif";
    std::string format = "bigtiff";
    int bits = 16;
    int blockSize = 4096;
    bool keepIcc = true;
    bool pyramid = true;
};

// name : "export"
// options : path (string), format (string), bits (int), blockSize (int), keepIcc (bool), pyramid (bool)
// pipelineOptions : { "type": "Mosaic", "key": "mosaic" }
// signature : aucune
class ExportStep : public Step {
public:
    explicit ExportStep(const ExportInput& input = {});
    bool execute() override;
};

} // namespace stitching::steps
