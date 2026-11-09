#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace StepsNs {

struct ExportInput {
    // Dossier de sortie. Si vide : dossier Téléchargements de l'utilisateur.
    std::string outputDirectory = "";
};

struct ExportOutput {
    std::filesystem::path imagePath;   // Chemin du TIFF composite
    cv::Size canvasSize;               // Dimensions globales en pixels
    std::size_t blockCount = 0;        // Nombre de blocs écrits
    bool isBigTiff = false;
};

// Écrit les blocs composites dans un unique TIFF tuilé (TIFF classique ou BigTIFF).
//
// Nom : "export". Paramètre : outputDirectory (string).
// Entrée : "CompositeBlocks" ("compositeBlocks").
// Sortie de fichier : mosaic.tif.
class ExportStepClass : public StepNs::StepClass {
public:
    explicit ExportStepClass(const ExportInput& initialInput = {});
    ~ExportStepClass() override = default;

    bool execute() override;
    Json externalInputsFingerprint() const override;
    const ExportOutput& getOutput() const { return output; }

private:
    ExportInput input;
    ExportOutput output;
};

} // namespace StepsNs
