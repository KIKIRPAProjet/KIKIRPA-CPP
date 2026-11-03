// include/backend/algorithms/seamline/graphcut/ImagePretreatment.h
#pragma once
#include <opencv2/opencv.hpp>
#include "core/data/algorithm_types.h"
#include <string>
#include <vector>

namespace Seamline {

// Même type que celui produit par le positionnement SIFT + RANSAC (core/Types.h)
using TilePositionData = DataNs::TilePositionData;

class ImagePretreatment {
public:
    // Prépare la liste des tuiles : tailles, positions (coins) et masques réduits.
    // Prend désormais en entrée la structure TilePositionData au lieu d'un fichier JSON.
    int processImages(const std::vector<TilePositionData>& input_positions,
                      const std::vector<std::string>& tile_paths,
                      double scale,
                      std::vector<cv::Size>& tile_sizes,
                      std::vector<cv::UMat>& masks_umat_small,
                      std::vector<cv::Point>& original_corners,
                      std::vector<cv::Point>& corners_small);

    // Extrait (ligne, colonne) d'un nom de fichier "..._rLL_cCC.tif(f)" et fabrique l'identifiant "rLL_cCC" (2 chiffres minimum).
    // Source unique de l'identifiant : utilisée par le positionnement (SIFT + RANSAC) ET par processImages.
    static bool parseTileName(const std::string& path, int& row, int& col, std::string& id);

    // Relit une tuile sur le disque et renvoie sa copie basse qualité
    static cv::Mat readSmallFloat(const std::string& path, double scale);

    // Image lue sans perte en CV_16UC3
    static cv::Mat read16(const std::string& path);
};

}  // namespace Seamline