// include/backend/steps/import_step.h
#pragma once

#include "core/data/tile.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>   // IWYU pragma: keep (type complet de ImportInput::tiles)
#include <nlohmann/json_fwd.hpp>

#include <string>

namespace StepsNs {

    using Json = nlohmann::json;

    // Valeurs initiales des paramètres du step.
    struct ImportInput {
        std::string path = "images/";   // dossier contenant les images
        std::string outputDirectory;   // dossier contenant les images
        // Expression régulière sur le nom du fichier : groupe 1 = colonne, groupe 2 = ligne (insensible à la casse).
        // Utilisée seulement si `tiles` est vide.
        std::string format = R"(img_C(\d+)_R(\d+)\.tif)";
        // Nombre de colonnes de la grille, utilisé seulement si `tiles` est vide et qu'aucun nom ne correspond à `format` :
        // les TIFF du dossier, triés par nom, remplissent alors la grille ligne par ligne. 0 : non donné.
        int columns = 0;
        // Liste des tuiles : [ { "file": "a.tif", "row": 0, "column": 0 }, ... ]. Vide : les tuiles sont les fichiers de
        // `path`, et leur case vient de leur nom (`format`) ou de l'ordre des noms (`columns`).
        Json tiles = Json::array();
    };

    // Sortie du step.
    struct ImportOutput {
        DataNs::TileList tiles;   // une fiche par image, triées ligne par ligne puis colonne par colonne
    };

    // Premier step d'un pipeline : repère les images et crée la fiche de chaque tuile (TileClass) : son identifiant,
    // son fichier et sa case (ligne, colonne) dans la grille. Les steps suivants n'utilisent plus que l'identifiant.
    // Deux façons de donner les tuiles :
    //   - `tiles` non vide : la liste donne, pour chaque tuile, son fichier (relatif à `path`, ou absolu) et sa case.
    //     Les noms de fichiers sont libres ;
    //   - `tiles` vide : les fichiers de `path` dont le nom correspond à `format`, qui donne la colonne et la ligne. Si
    //     aucun nom ne correspond, les TIFF de `path`, triés par nom, remplissent la grille ligne par ligne sur
    //     `columns` colonnes : le nom des fichiers n'est jamais exigé.
    // Dans les deux cas, l'identifiant d'une tuile est son rang dans l'ordre ligne par ligne, puis colonne par colonne
    // (0, 1, 2...), calculé par le step.
    // Pour chaque image, il écrit un TIFF tuilé pyramidal
    // <outputDirectory>/.cache/pyramids/<nom>_<empreinte du chemin>_pyramid.tif
    // (ou <path>/.cache/pyramids si outputDirectory est vide).
    // (voir core/io/pyramid_cache.h), ou réutilise celui qui a été produit à partir de la version actuelle de l'image
    // (par un import précédent ou par l'interface) ; les steps suivants lisent la tuile à partir de lui, pas de
    // l'image d'origine. Les voisines (gauche, droite, haut, bas) sont déduites des cases. Une seule image est en
    // mémoire à la fois.
    //
    // Nom : "import".   Paramètres : path (string), format (string), columns (int), tiles (json).
    // Entrées : aucune.   Sortie : "Tiles" (clé "tiles").
    // getResult() : "tiles" (nombre de tuiles) et "placement" ("list", "fileName" ou "nameOrder"), ou "error".
    // execute() renvoie false avec la cause dans getResult()["error"] si le dossier n'existe pas, si le format est
    // invalide (il doit avoir 2 groupes), si aucun nom ne correspond à `format` et que `columns` n'est pas donné, si le
    // dossier n'a aucun TIFF, si la liste `tiles` est mal formée, si deux tuiles occupent la même case, ou si une image
    // ou un TIFF est illisible.
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
