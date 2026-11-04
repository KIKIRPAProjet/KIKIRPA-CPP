// include/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.h
#pragma once

#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace GraphCutNs {

    // Étiquette d'un pixel couvert par aucune tuile : la carte de provenance est sur 8 bits (comme l'ancien fond à 255).
    constexpr uchar NO_TILE = 255;


    // Résultat de la vérification du pavage
    struct SeamCheck {
        bool isExactTiling = false;      // les polygones redessinés redonnent exactement la carte de provenance
        long differentPixelCount = 0;
        long overlapPixelCount = 0;    // pixels dessinés par deux territoires (doit valoir 0)
        long gapPixelCount = 0;        // pixels couverts par une tuile mais attribués à aucune (doit valoir 0)
        long fallbackPixelCount = 0;   // pixels réattribués à une tuile voisine à l'assemblage (arrondis de la grille réduite)
        int polygonCount = 0;
        int holeCount = 0;
        long vertexCount = 0;
    };

    class GraphCutSeamFinderClass {

        public:
            // Coupe de graphe d'OpenCV (COST_COLOR) appliquée paire par paire.
            // OpenCV traite déjà les tuiles deux à deux (i < j, masques mis à jour sur place) : on l'appelle donc sur 2 tuiles
            // à la fois, dans le même ordre, ce qui donne le même résultat que l'appel unique. Pour chaque paire, les deux tuiles
            // sont relues depuis le disque (tilePaths), réduites du facteur `scale`, puis libérées : jamais plus de deux tuiles
            // en mémoire (la tuile i reste chargée pendant qu'on parcourt les j). Seuls les masques (8 bits) persistent.
            // L'assemblage reste exact : chaque pixel de la mosaïque reçoit une seule tuile (carte de provenance indexMatrix,
            // NO_TILE = aucune), choisie parmi les tuiles qui le couvrent exactement (rectangles issus des positions).
            //
            // masksUmatSmall : masques réduits de chaque tuile, mis à jour par la coupe (zones gardées).
            // indexMatrix    : sortie, carte de provenance pleine résolution (CV_8U).
            // Renvoie false si les entrées sont incohérentes (tailles de listes, nombre de tuiles) ou si une tuile ne peut
            // pas être relue : la cause est alors dans getErrorMessage(). Les exceptions d'OpenCV ne sont pas interceptées.
            bool computeSeamline(const std::vector<std::string>& tilePaths,
                                 const std::vector<cv::Size>& tileSizes,
                                 const std::vector<cv::Point>& originalCorners,
                                 double scale,
                                 const std::vector<cv::Point>& cornersSmall,
                                 std::vector<cv::UMat>& masksUmatSmall,
                                 cv::Mat& indexMatrix);

            // Polygones de chaque territoire, trous compris (RETR_CCOMP), sans filtre d'aire : le pavage doit rester exact.
            std::vector<DataNs::TileTerritory> extractPolygons(const cv::Mat& indexMatrix, std::size_t tileCount) const;

            // Redessine les territoires et compare à la carte de provenance (pavage exact, sans trou ni chevauchement ?).
            SeamCheck verifyTiling(const std::vector<DataNs::TileTerritory>& territories,
                                   const cv::Mat& indexMatrix,
                                   const std::vector<cv::Size>& tileSizes,
                                   const std::vector<cv::Point>& originalCorners) const;

            // Cause du dernier échec de computeSeamline() ; vide si le dernier appel a réussi.
            const std::string& getErrorMessage() const {
                return errorMessage;
            }

        private:
            long fallbackPixelCount = 0;   // renseigné par computeSeamline
            std::string errorMessage;

    };

}
