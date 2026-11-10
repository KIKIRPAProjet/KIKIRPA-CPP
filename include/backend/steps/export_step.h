// include/backend/steps/export_step.h
#pragma once

#include "core/step/step.h"

#include <opencv2/core/types.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace StepsNs {

    using Json = StepNs::Json;

    struct ExportInput {
        // Vide : dossier Téléchargements de l'utilisateur courant.
        std::string outputDirectory;
    };

    struct ExportOutput {
        std::filesystem::path imagePath;   // TIFF composite
        std::filesystem::path jpegPath;    // JPEG aperçu
        cv::Size canvasSize;               // dimensions globales en pixels
        std::size_t blockCount = 0;        // blocs écrits
        bool isBigTiff = false;
    };

    // Écrit les blocs composites dans un TIFF tuilé et un aperçu JPEG.
    //
    // Nom : "export". Paramètre : outputDirectory (string, vide = Téléchargements).
    // Entrée : "CompositeBlocks" ("compositeBlocks").
    // Sorties de fichiers : mosaic.tif et mosaic.jpg.
    // Ce step est terminal : il ne publie pas de donnée pour un step suivant.
    class ExportStepClass : public StepNs::StepClass {

        public:
            explicit ExportStepClass(const ExportInput& initialInput = {});
            bool execute() override;
            // Empreinte des fichiers exportés, pour refaire ce step si un résultat a disparu ou changé.
            Json externalInputsFingerprint() const override;
            // Chemins et dimensions renseignés après une exécution réussie.
            const ExportOutput& getOutput() const { return output; }

        private:
            ExportOutput output;

    };

}
