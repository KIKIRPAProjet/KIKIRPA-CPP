// include/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.h
#pragma once

#include "core/data/algorithm_types.h"
#include <opencv2/core/hal/interface.h> 
#include <string>
#include <vector>
// Inclusions directes des types OpenCV pour satisfaire misc-include-cleaner
#include <opencv2/core/types.hpp> // Pour cv::Size, cv::Point
#include <opencv2/core/mat.hpp>   // Pour cv::Mat, cv::UMat

namespace GraphCutNs {

    // Étiquette d'un pixel couvert par aucune tuile : la carte de provenance est sur 8 bits (comme l'ancien fond à 255).
    constexpr uchar NO_TILE = 255;

    // Géométrie pleine résolution des tuiles (une entrée par tuile).
    struct FullResolutionGeometry {
        const std::vector<cv::Size>& tileSizes;
        const std::vector<cv::Point>& originalCorners;
    };

    // Tuiles réduites de `scale` : ce que lit la coupe de graphe. Les masques sont mis à jour sur place.
    struct ReducedGrid {
        double scale;
        const std::vector<cv::Point>& cornersSmall;
        std::vector<cv::UMat>& masksUmatSmall;
    };
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
            // à la fois, dans le même ordre, ce qui donne le même résultat que l'appel unique. Chaque tuile est lue depuis le disque
            // (tilePaths) et réduite du facteur `scale` une seule fois, à sa première paire, puis libérée dès que plus aucune paire
            // à venir ne l'utilise : en mémoire, seules les tuiles voisines de la ligne courante (environ une ligne de tuiles
            // réduites, plus deux). Les masques (8 bits) persistent.
            // L'assemblage reste exact : chaque pixel de la mosaïque reçoit une seule tuile (carte de provenance indexMatrix,
            // NO_TILE = aucune), choisie parmi les tuiles qui le couvrent exactement (rectangles issus des positions).
            //
            // masksUmatSmall : masques réduits de chaque tuile, mis à jour par la coupe (zones gardées).
            // indexMatrix    : sortie, carte de provenance pleine résolution (CV_8U).
            // Renvoie false si les entrées sont incohérentes (tailles de listes, nombre de tuiles) ou si une tuile ne peut
            // pas être relue : la cause est alors dans getErrorMessage(). Les exceptions d'OpenCV ne sont pas interceptées.
            bool computeSeamline(const std::vector<std::string>& tilePaths,
                                const FullResolutionGeometry& fullResolution,
                                ReducedGrid& reducedGrid,
                                cv::Mat& indexMatrix);

            // Polygones de chaque territoire, trous compris (RETR_CCOMP), sans filtre d'aire : le pavage doit rester exact.
            // tileRects : rectangle de chaque tuile dans la mosaïque (même ordre que les tuiles). Le territoire d'une tuile
            // est cherché dans son seul rectangle : à utiliser de préférence, le coût ne dépend plus de la taille de la
            // mosaïque.
            std::vector<DataNs::TileTerritory> extractPolygons(const cv::Mat& indexMatrix,
                                                               const std::vector<cv::Rect>& tileRects) const;

            // Cause du dernier échec de computeSeamline() ; vide si le dernier appel a réussi.
            const std::string& getErrorMessage() const {
                return errorMessage;
            }

        private:
            long fallbackPixelCount = 0;   // renseigné par computeSeamline
            std::string errorMessage;

    };

}
