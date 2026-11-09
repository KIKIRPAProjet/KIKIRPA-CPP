// include/backend/algorithms/cropping/mosaic_crop.h
#pragma once

#include "core/data/mosaic_files.h"

#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

namespace CroppingNs {

    // Informations nécessaires pour rogner une mosaïque.
    struct CropInput {

        // Mosaïque produite par l'assemblage.
        DataNs::MosaicFiles mosaic;

        // Rectangle à conserver, en pixels pleine résolution.
        // Même repère que les coordonnées des noms de fichiers sources.
        // Les coordonnées peuvent être négatives si l'assemblage en produit.
        // Les bords droit et inférieur sont exclus.
        cv::Rect rectangle;

        // Nouveau dossier recevant le résultat.
        // Il ne doit pas exister ; son dossier parent doit exister.
        std::filesystem::path outputDirectory;

    };

    // Produit une mosaïque rognée au même format de fichiers que l'assemblage :
    // rgb_x<X>_y<Y>.png, provenance_x<X>_y<Y>.png et mask_x<X>_y<Y>.png.
    //
    // Le rectangle doit être non vide et entièrement couvert par la mosaïque.
    // Son coin supérieur gauche devient l'origine (0, 0) du résultat.
    // Les blocs de sortie ne sont pas réalignés sur une grille.
    //
    // Les blocs extérieurs sont exclus.
    // Les blocs entièrement conservés sont copiés sans décoder leurs pixels.
    // Les blocs partiellement conservés sont découpés, un fichier à la fois,
    // en conservant leur profondeur et leurs canaux.
    // Les fichiers sources ne sont jamais modifiés.
    //
    // Renvoie true et renseigne output en cas de succès.
    // Renvoie false, vide output et renseigne errorMessage en anglais
    // en cas d'échec. errorMessage est vide en cas de succès.
    //
    // Le dossier créé par cet appel est supprimé en cas d'échec,
    // dans la mesure où le système de fichiers le permet.
    // Aucun dossier de sortie existant n'est écrasé.
    bool cropMosaic(
        const CropInput& input,
        DataNs::MosaicFiles& output,
        std::string& errorMessage
    );

}