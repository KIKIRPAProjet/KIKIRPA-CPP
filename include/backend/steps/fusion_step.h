// include/backend/steps/fusion_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <opencv2/core/types.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace StepsNs {

    using Json = StepNs::Json;

    struct FusionInput {
        // Dossier des trois TIFF de la mosaïque. Vide : <dossier des images>/.cache/mosaic, à côté du cache d'import.
        std::string outputDirectory;
    };

    // Sortie du step : la description de la mosaïque écrite sur disque, et sa clé et son type dans le DataStore.
    struct FusionOutput {
        std::string key = "mosaic";
        DataNs::Mosaic value;
        std::string type = "Mosaic";
    };

    // Un polygone du territoire d'une tuile (contour extérieur et trous, dans le repère de la mosaïque), avec son
    // rectangle englobant : un polygone loin du bloc n'est pas dessiné.
    struct TerritoryPolygon {
        std::vector<cv::Point> outline;
        std::vector<std::vector<cv::Point>> holes;
        cv::Rect boundingBox;
    };

    // Une tuile placée dans la mosaïque, avec tout ce qu'il faut pour en tirer les pixels d'un bloc.
    struct PlacedTile {
        int tileId = 0;                         // TileClass::getId(), écrit dans la provenance
        std::filesystem::path tiffPath;         // TIFF pyramidal de la tuile ; seul le niveau 0 est lu
        cv::Rect mosaicRect;                    // place de la tuile dans la mosaïque (repère des territoires)
        double gain = 1.0;                      // correction : pixels divisés par gain · exp(fieldStrength · r²)
        double fieldStrength = 0.0;
        std::vector<TerritoryPolygon> territory;
    };

    // Tuiles placées et dimensions de la mosaïque, dans le repère des territoires (DataNs::computeMosaicCorners).
    struct FusionContext {
        std::vector<PlacedTile> tiles;
        cv::Size canvasSize;
        std::vector<std::uint8_t> iccProfile;
    };

    // Calcule le bloc `blockRect` de la mosaïque : chaque pixel prend la valeur de la tuile dont le territoire le
    // contient, corrigée en intensité et convertie en RGB 16 bits. Ne lit, dans le TIFF de chaque tuile, que
    // l'intersection tuile / bloc, et seulement si le territoire de la tuile touche le bloc. Pixel sans territoire :
    // image et provenance à 0, masque à 0. Lève std::runtime_error si un TIFF ne peut pas être lu.
    DataNs::CompositeBlock renderBlock(const cv::Rect& blockRect, const FusionContext& context);

    // Assemblage : écrit la mosaïque pleine résolution bloc par bloc (DataNs::BLOCK_SIZE), sans jamais la garder en
    // mémoire. Chaque bloc est calculé par renderBlock puis écrit aussitôt dans trois TIFF tuilés (image RGB 16 bits,
    // provenance 16 bits, masque 8 bits) avec IONs::TiledTiffWriterClass. Le DataStore ne reçoit que leurs chemins.
    //
    // Nom : "fusion". Paramètre : outputDirectory (string, vide = <dossier des images>/.cache/mosaic).
    // Entrées : "Tiles" ("tiles"), "Positions" ("positions"), "Territories" ("territories") ; "intensityCorrection"
    // est appliquée si un step précédent l'a produite (lue sans être déclarée : le moteur n'a pas d'entrée facultative).
    // Sortie : "Mosaic" ("mosaic").
    class FusionStepClass : public StepNs::StepClass {

        public:
            explicit FusionStepClass(const FusionInput& initialInput = {});
            bool execute() override;
            // Empreinte des trois TIFF produits : le step est refait si l'un d'eux disparaît ou change. Sans
            // outputDirectory, elle n'est connue qu'après execute() (nulle avant).
            Json externalInputsFingerprint() const override;
            const FusionOutput& getOutput() const { return output; }

        private:
            FusionOutput output;

    };

}
