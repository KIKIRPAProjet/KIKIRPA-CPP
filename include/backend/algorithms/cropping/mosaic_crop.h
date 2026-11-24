// include/backend/algorithms/cropping/mosaic_crop.h
#pragma once

#include "core/data/algorithm_types.h"

#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>

namespace CroppingNs {

    struct CropInput {
        DataNs::Mosaic mosaic;
        // Pixels pleine résolution ; rectangle non vide, inclus dans canvasSize.
        // Les bords droit et inférieur sont exclus.
        cv::Rect rectangle;
        // Dossier nouveau ou résultat identifié de mosaic-crop.
        // Son parent doit déjà exister. Il ne doit pas contenir les sources.
        std::filesystem::path outputDirectory;
    };

    // Découpe les trois TIFF ensemble et conserve leurs types et l'ordre RGB.
    // L'origine de sortie devient (0, 0), sa taille devient rectangle.size().
    // Travaille par blocs, sans modifier les sources.
    //
    // Prépare les trois fichiers dans un dossier temporaire voisin avant
    // publication. Un ancien résultat identifié de mosaic-crop peut être
    // remplacé, même si un TIFF manque. Un dossier étranger est refusé.
    //
    // En cas d'échec : output est vide et errorMessage décrit la cause en anglais.
    // Si la publication échoue, l'ancien résultat est restauré si possible.
    // Si la restauration échoue, la sauvegarde est conservée et son chemin
    // est indiqué dans errorMessage.
    //
    // Les exécutions simultanées vers le même dossier ne sont pas prises en charge.
    bool cropMosaic(const CropInput& input, DataNs::Mosaic& output,
        std::string& errorMessage);

}