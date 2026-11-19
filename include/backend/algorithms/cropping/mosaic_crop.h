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
        // Nouveau dossier, inexistant ; son parent doit déjà exister.
        std::filesystem::path outputDirectory;
    };

    // Découpe les trois TIFF ensemble et conserve leurs types et l'ordre RGB.
    // L'origine de sortie devient (0, 0), sa taille devient rectangle.size().
    // Travaille par blocs, sans modifier les sources ni écraser un dossier existant.
    // En cas d'échec : output est vide, errorMessage contient la cause en anglais,
    // et le dossier créé par l'appel est supprimé si le système le permet.
    bool cropMosaic(const CropInput& input, DataNs::Mosaic& output,
        std::string& errorMessage);

}