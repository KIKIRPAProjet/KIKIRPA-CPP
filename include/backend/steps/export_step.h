// include/backend/steps/export_step.h
#pragma once

#include "core/step/step.h"

#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

namespace StepsNs {

    using Json = StepNs::Json;

    struct ExportInput {
        static constexpr int DEFAULT_PREVIEW_MAX_SIDE = 6000;

        // Vide : dossier Téléchargements de l'utilisateur courant.
        std::string outputDirectory;
        // Nom du fichier de sortie. Vide : "mosaic.tif" ou "mosaic.jpg" selon format.
        std::string filename;
        // "tiff" conserve la mosaïque RGB 16 bits ; "jpeg" produit un aperçu RGB 8 bits réduit ; "both" produit les deux.
        std::string format = "tiff";
        int previewMaxSide = DEFAULT_PREVIEW_MAX_SIDE;
    };

    struct ExportOutput {
        std::filesystem::path imagePath;   // Image composite publiée
        cv::Size canvasSize;               // dimensions globales en pixels
        bool isBigTiff = false;
        std::string format = "tiff";
    };

    // Publie l'image de la mosaïque assemblée sous outputDirectory/<filename>, ou son prochain numéro disponible.
    // Le format TIFF est copié tel quel ; le format JPEG convertit la mosaïque RGB 16 bits en RGB 8 bits.
    // Nom : "export". Paramètres : outputDirectory, filename, format ("tiff" par défaut ou "jpeg").
    // Entrée : "Mosaic" ("mosaic"). Sortie : mosaic.tif/mosaic.jpg ou nom incrémenté.
    //
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