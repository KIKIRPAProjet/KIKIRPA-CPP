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
        // Supprimer le cache de la mosaïque (les trois TIFF de `fusion`) après un export réussi. Faux par
        // défaut : voir le header pour ce que ça coûte à une reprise.
        bool removeMosaicCache = false;
    };

    struct ExportOutput {
        std::filesystem::path imagePath;   // TIFF composite publié
        cv::Size canvasSize;               // dimensions globales en pixels
        bool isBigTiff = false;
    };

    // Publie l'image de la mosaïque assemblée sous outputDirectory/<filename> (défaut mosaic.tif) : contrôle de son
    // en-tête (dimensions, RGB 16 bits tuilé), puis copie sous un nom temporaire et renommage. Les pixels ne passent pas
    // par la mémoire. Si un export de ce nom existe déjà, le fichier prend le numéro qui suit le plus élevé présent :
    // " (1)", " (2)"... Le checkpoint du step reste valable tant que ce dernier export est là, inchangé.
    //
    // Avec removeMosaicCache, les trois TIFF de `fusion` (<dossier des images>/.cache/mosaic) sont supprimés après
    // une copie réussie, et leur dossier s'il ne reste rien : l'image publiée est une copie exacte de la mosaïque,
    // et `fusion` sait la refaire. Sur une grille 10 x 10, c'est 40 à 60 Gio récupérés. Jamais le fichier qui vient
    // d'être publié : si le dossier de sortie tombe sur le cache, la destination peut être l'un des trois, et
    // copyFileReplacing n'a alors rien copié. Deux conséquences à connaître, et la raison du défaut à faux :
    //   - une **reprise** (`--resume`) refait `fusion`, dont l'empreinte porte sur ces trois fichiers, puis refait
    //     l'export, qui écrit une copie de plus (" (1)") ;
    //   - l'aperçu de la mosaïque de l'interface n'a plus rien à lire.
    //
    // Nom : "export". Paramètres : outputDirectory (string, vide = Téléchargements), filename (string, vide =
    // mosaic.tif), removeMosaicCache (bool, faux par défaut).
    // Entrée : "Mosaic" ("mosaic"). Sortie de fichier : mosaic.tif ou nom incrémenté (ni provenance ni masque).
    // getResult() : "image", "canvasSize" et, avec removeMosaicCache, "removedMosaicCacheFiles".
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
