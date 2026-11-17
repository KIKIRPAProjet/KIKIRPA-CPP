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
        // Nom du fichier de sortie. Vide : "mosaic.tif".
        std::string filename;
    };

    struct ExportOutput {
        std::filesystem::path imagePath;   // TIFF composite publié
        cv::Size canvasSize;               // dimensions globales en pixels
        bool isBigTiff = false;
    };

    // Publie l'image de la mosaïque assemblée sous outputDirectory/<filename> (défaut mosaic.tif) : contrôle de son
    // en-tête (dimensions, RGB 16 bits tuilé), puis copie sous un nom temporaire et renommage. Les pixels ne passent pas
    // par la mémoire. Si un fichier du même nom existe déjà, un suffixe incrémental " (1)", " (2)"... est ajouté.
    //
    // Nom : "export". Paramètres : outputDirectory (string, vide = Téléchargements), filename (string, vide = mosaic.tif).
    // Entrée : "Mosaic" ("mosaic"). Sortie de fichier : mosaic.tif ou nom incrémenté (ni provenance ni masque).
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
