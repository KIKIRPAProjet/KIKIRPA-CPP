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

    // Exporte une région (ou toute l'image si region est vide) d'un TIFF tuilé RGB
    // vers un fichier JPEG Baseline 8 bits en streaming par bandes de BLOCK_SIZE lignes.
    // Si la région dépasse 65 500 px de côté, elle est sous-échantillonnée à la volée.
    bool exportTiffRegionToBaselineJpeg(const std::filesystem::path& tiffPath,
                                        const cv::Rect& region,
                                        const std::filesystem::path& outputPath,
                                        std::string& errorMessage);

    // Publie l'image de la mosaïque assemblée sous outputDirectory/<filename> (défaut mosaic.tif) : contrôle de son
    // en-tête (dimensions, RGB 16 bits tuilé), puis copie sous un nom temporaire et renommage (ou conversion en JPEG
    // Baseline 8 bits en streaming si filename se termine par .jpg / .jpeg). Les pixels ne sont jamais chargés en entier
    // en mémoire. Si un export de ce nom existe déjà, le fichier prend le numéro qui suit le plus élevé présent :
    // " (1)", " (2)"... Le checkpoint du step reste valable tant que ce dernier export est là, inchangé.
    //
    // Nom : "export". Paramètres : outputDirectory (string, vide = Téléchargements), filename (string, vide = mosaic.tif).
    // Entrée : "Mosaic" ("mosaic"). Sortie de fichier : mosaic.tif/mosaic.jpg ou nom incrémenté (ni provenance ni masque).
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
