#pragma once
#include "core/Step.h"
#include "core/Tuile.h"
#include <string>

namespace stitching::steps {

// Entrée du step = ses options
struct ImportInput {
    std::string path = "images/";
    // Regex sur le nom du fichier : groupe 1 = colonne, groupe 2 = ligne
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
    // Nom, taille et date des fichiers source : une image ajoutée, retirée ou modifiée périme le checkpoint
    Json inputFingerprint() const override;
    const ImportOutput& getOutput() const { return output; }
};

} // namespace stitching::steps
