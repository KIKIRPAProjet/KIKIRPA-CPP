// include/backend/core/io/pyramidal_tiff_writer.h
#pragma once

#include <opencv2/core/mat.hpp>

#include <filesystem>
#include <string>

namespace IONs {

    // Écrit `image` dans `outputPath` en TIFF tuilé pyramidal : le niveau 0, puis les niveaux réduits de moitié
    // successivement (LEVEL_COUNT niveaux au total), en blocs de BLOCK_SIZE pixels compressés en Deflate avec
    // prédicteur horizontal.
    // Image acceptée : matrice 2D non vide, CV_8U ou CV_16U, avec 1, 3 ou 4 canaux. Crée le dossier de sortie si besoin.
    // Les pixels sont écrits tels quels, et le TIFF déclare PHOTOMETRIC_RGB dès qu'il y a plusieurs canaux :
    // **l'appelant doit donc fournir du RGB** (RGBA à 4 canaux), et non l'ordre BGR de cv::imread. Le cache d'import
    // convertit pour cette raison (core/io/pyramid_cache.h).
    // `levelCount` : nombre de niveaux à écrire, 0 (par défaut) pour DataNs::LEVEL_COUNT. Le cache l'abaisse
    // d'un quand le niveau 0 est lu dans l'image de l'œuvre : `image` est alors déjà la demi-résolution.
    // Renvoie false avec la cause dans errorMessage ; le fichier incomplet est alors supprimé.
    bool createPyramidalTiledTiff(
        const cv::Mat& image,
        const std::filesystem::path& outputPath,
        std::string& errorMessage,
        int levelCount = 0
    );

}
