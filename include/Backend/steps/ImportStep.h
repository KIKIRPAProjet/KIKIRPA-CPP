#pragma once
#include "core/Step.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct ImportInput {
    std::string path = "images/";
    // Regex sur le nom du fichier : groupe 1 = colonne, groupe 2 = ligne
    // ex. images/img_C00_R00.tif, images/img_C01_R00.tif
    std::string format = R"(img_C(\d+)_R(\d+)\.tif)";
};

// Sortie du step = sa signature
struct ImportOutput {
    Tiles tiles;
};

// name : "import"
// options : path (string), format (string)
// pipelineOptions : aucune
// signature : { "type": "Tiles", "key": "tiles" }
class ImportStep : public Step {
    ImportOutput output;
public:
    explicit ImportStep(const ImportInput& input = {});
    bool execute() override;
    const ImportOutput& getOutput() const { return output; }
};

} // namespace stitching::steps
