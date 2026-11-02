#pragma once
#include "core/data/stitching_types.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace Seamline {

constexpr uchar NO_TILE = 255;   // étiquette d'un pixel couvert par aucune tuile (comme l'ancien fond à 255)

// Défini dans core/data/stitching_types.h (partagé avec les checkpoints) ; alias pour garder le nom Seamline::TileTerritory
using TileTerritory = DataNs::TileTerritory;

// Résultat de la vérification du pavage
struct SeamCheck {
    bool exactTiling = false;      // les polygones redessinés redonnent exactement la carte de provenance
    long differentPixels = 0;
    long overlapPixels = 0;        // pixels dessinés par deux territoires (doit valoir 0)
    long gapPixels = 0;            // pixels couverts par une tuile mais attribués à aucune (doit valoir 0)
    long fallbackPixels = 0;       // pixels réattribués à une tuile voisine à l'assemblage (arrondis de la grille réduite)
    int polygons = 0, holes = 0;
    long vertices = 0;
};

class GraphCut {
public:
    // Coupe de graphe d'OpenCV (COST_COLOR) appliquée PAIRE PAR PAIRE.
    // OpenCV traite déjà les tuiles deux à deux (i < j, masques mis à jour sur place) : on l'appelle donc sur 2 tuiles
    // à la fois, dans le même ordre, ce qui donne le même résultat que l'appel unique. Pour chaque paire, les DEUX tuiles
    // sont relues depuis le disque (tile_paths), réduites du facteur `scale`, puis libérées : jamais plus de deux tuiles
    // en mémoire (la tuile i reste chargée pendant qu'on parcourt les j). Seuls les masques (8 bits) persistent.
    // L'ASSEMBLAGE reste exact : chaque pixel de la mosaïque reçoit UNE seule tuile (carte de provenance index_matrix,
    // 255 = aucune), choisie parmi les tuiles qui le couvrent exactement (rectangles issus des positions).
    // final_mosaic : CV_8UC3 (affichage seulement, converti depuis les 16 bits, tuile par tuile).
    int computeSeamline(const std::vector<std::string>& tile_paths,
                        const std::vector<cv::Size>& tile_sizes,
                        const std::vector<cv::Point>& original_corners,
                        double scale,
                        std::vector<cv::Point>& corners_small,
                        std::vector<cv::UMat>& masks_umat_small,
                        cv::Mat& final_mosaic,
                        cv::Mat& index_matrix);

    // Polygones de chaque territoire, trous compris (RETR_CCOMP), sans filtre d'aire : le pavage doit rester exact.
    std::vector<TileTerritory> extractPolygons(const cv::Mat& index_matrix, int num_tiles);

    // Redessine les territoires et compare à la carte de provenance (pavage exact, sans trou ni chevauchement ?).
    SeamCheck verifyTiling(const std::vector<TileTerritory>& territories,
                           const cv::Mat& index_matrix,
                           const std::vector<cv::Size>& tile_sizes,
                           const std::vector<cv::Point>& original_corners) const;

    long fallbackPixels = 0;   // renseigné par computeSeamline
};

}  // namespace Seamline