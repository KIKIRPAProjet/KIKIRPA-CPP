// include/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.h
#pragma once

#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace GraphCutNs {

    constexpr uchar NO_TILE = 255;   // étiquette d'un pixel couvert par aucune tuile (comme l'ancien fond à 255)

    // Résultat de la vérification du pavage
    struct SeamCheck {
        bool isExactTiling = false;      // les polygones redessinés redonnent exactement la carte de provenance
        long differentPixelCount = 0;
        long overlapPixelCount = 0;      // pixels dessinés par deux territoires (doit valoir 0)
        long gapPixelCount = 0;          // pixels couverts par une tuile mais attribués à aucune (doit valoir 0)
        long fallbackPixelCount = 0;     // pixels réattribués à une tuile voisine à l'assemblage (arrondis de la grille
                                         // réduite)
        int polygonCount = 0, holeCount = 0;
        long vertexCount = 0;
    };

    class GraphCutSeamFinderClass {

        public:
            // Coupe de graphe d'OpenCV (COST_COLOR) appliquée PAIRE PAR PAIRE.
            // OpenCV traite déjà les tuiles deux à deux (i < j, masques mis à jour sur place) : on l'appelle donc sur 2
            // tuiles à la fois, dans le même ordre, ce qui donne le même résultat que l'appel unique. Pour chaque paire,
            // les DEUX tuiles sont relues depuis le disque (tilePaths), réduites du facteur `scale`, puis libérées : jamais
            // plus de deux tuiles en mémoire (la tuile i reste chargée pendant qu'on parcourt les j). Seuls les masques
            // (8 bits) persistent.
            // L'ASSEMBLAGE reste exact : chaque pixel de la mosaïque reçoit UNE seule tuile (carte de provenance
            // tileIndexMap, NO_TILE = aucune), choisie parmi les tuiles qui le couvrent exactement (rectangles issus des
            // positions).
            // finalMosaic : CV_8UC3 (affichage seulement, converti depuis les 16 bits, tuile par tuile).
            // Renvoie 1 en cas de succès, -1 en cas d'erreur (entrées incohérentes, 255 tuiles ou plus, lecture impossible).
            int computeSeamline(const std::vector<std::string>& tilePaths, const std::vector<cv::Size>& tileSizes,
                                const std::vector<cv::Point>& originalCorners, double scale,
                                std::vector<cv::Point>& smallCorners, std::vector<cv::UMat>& smallMasks, cv::Mat& finalMosaic,
                                cv::Mat& tileIndexMap);

            // Polygones de chaque territoire, trous compris (RETR_CCOMP), sans filtre d'aire : le pavage doit rester exact.
            std::vector<DataNs::TileTerritory> extractPolygons(const cv::Mat& tileIndexMap, int tileCount);

            // Redessine les territoires et compare à la carte de provenance (pavage exact, sans trou ni chevauchement ?).
            SeamCheck verifyTiling(const std::vector<DataNs::TileTerritory>& territories, const cv::Mat& tileIndexMap,
                                   const std::vector<cv::Size>& tileSizes, const std::vector<cv::Point>& originalCorners) const;

            long fallbackPixelCount = 0;   // renseigné par computeSeamline

    };

}
