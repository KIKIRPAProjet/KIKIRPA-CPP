// include/backend/steps/export_step.h
#pragma once

#include "core/step/step.h"

#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

namespace StepsNs {

    using Json = StepNs::Json;

    struct ExportInput {
        // Vide : dossier Téléchargements de l'utilisateur courant.
        std::string outputDirectory;
        // "tiff" publie la mosaïque sans conversion ; "jpeg" produit une image JPEG 8 bits.
        std::string format = "tiff";
    };

    struct ExportOutput {
        std::filesystem::path imagePath;   // Image composite publiée
        cv::Size canvasSize;               // dimensions globales en pixels
        bool isBigTiff = false;
        std::string format = "tiff";
    };

    // Publie l'image de la mosaïque assemblée sous outputDirectory/mosaic.tif ou mosaic.jpg. Le format TIFF contrôle
    // l'en-tête puis copie le fichier ; le format JPEG convertit la mosaïque RGB 16 bits en RGB 8 bits en mémoire.
    //
    // Nom : "export". Paramètres : outputDirectory (string, vide = Téléchargements), format ("tiff" par défaut ou
    // "jpeg"). Entrée : "Mosaic" ("mosaic"). Sortie de fichier : mosaic.tif ou mosaic.jpg (ni provenance ni masque).
    // Ce step est terminal : il ne publie pas de donnée pour un step suivant.
    class ExportStepClass : public StepNs::StepClass {

        public:
            explicit ExportStepClass(const ExportInput& initialInput = {});
            bool execute() override;
            // Empreinte du fichier exporté, pour refaire ce step si le résultat a disparu ou changé.
            Json externalInputsFingerprint() const override;
            // Chemin et dimensions renseignés après une exécution réussie.
            const ExportOutput& getOutput() const { return output; }

        private:
            ExportOutput output;

    };

}
