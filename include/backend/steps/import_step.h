// include/backend/steps/import_step.h
#pragma once

#include "core/data/tile.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>

#include <string>

namespace StepsNs {

    using Json = nlohmann::json;

    // Valeurs initiales des paramètres du step.
    struct ImportInput {
        std::string path = "images/";   // dossier contenant les images
        // Expression régulière sur le nom du fichier : groupe 1 = colonne, groupe 2 = ligne (insensible à la casse)
        std::string format = R"(img_C(\d+)_R(\d+)\.tif)";
    };

    // Sortie du step.
    struct ImportOutput {
        DataNs::TileList tiles;   // une fiche par image, triées ligne par ligne puis colonne par colonne
    };

    // Premier step d'un pipeline : repère les images d'un dossier et crée la fiche de chaque tuile (TileClass).
    // Pour chaque image, il écrit un TIFF tuilé pyramidal dans <path>/.cache/pyramids/ (voir
    // createPyramidalTiledTiff) ; les steps suivants lisent la tuile à partir de lui, pas de l'image d'origine. Les
    // voisines (gauche, droite, haut, bas) sont déduites de la colonne et de la ligne lues dans le nom du fichier.
    // Une seule image est en mémoire à la fois.
    //
    // Nom : "import".   Paramètres : path (string), format (string).
    // Entrées : aucune.   Sortie : "Tiles" (clé "tiles").
    // execute() renvoie false avec la cause dans getResult()["error"] si le dossier n'existe pas, si le format est
    // invalide (il doit avoir 2 groupes), si aucun fichier ne correspond, ou si une image ou un TIFF est illisible.
    class ImportStepClass : public StepNs::StepClass {

        public:
            explicit ImportStepClass(const ImportInput& initialInput = {});
            bool execute() override;
            // Nom, taille et date des fichiers source : une image ajoutée, retirée ou modifiée périme le checkpoint.
            Json externalInputsFingerprint() const override;
            const ImportOutput& getOutput() const { return output; }

        private:
            ImportOutput output;

    };

}
