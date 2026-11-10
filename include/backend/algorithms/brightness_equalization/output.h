// include/backend/algorithms/brightness_equalization/output.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>

namespace BrightnessEqualizationNs {

    // Place d'une région dans sa tuile : le champ d'éclairage dépend de la position dans la tuile entière.
    struct RegionInTile {
        cv::Point origin;    // coin haut-gauche de la région, en pixels de la tuile
        cv::Size tileSize;   // dimensions de la tuile entière
    };

    // Étape 3 : application de la correction aux tuiles en pleine résolution, et bilan lisible.
    class OutputClass {

        public:
            // tile / (gain · exp(fieldStrength · r²)), pixel par pixel : multiplication pure par un champ lisse, aucun
            // filtre (grain et craquelures conservés). Même facteur sur les canaux couleur (teinte conservée), alpha
            // recopié. Entier 8 ou 16 bits → 16 bits (8 bits multiplié par 257, comme le notebook) ; flottant → même
            // type.
            static cv::Mat correctTile(const cv::Mat& tile, double gain, double fieldStrength);

            // Comme correctTile, pour une région seulement de la tuile : r² est calculé dans le repère de la tuile
            // entière (placement), si bien que corriger une tuile région par région donne exactement correctTile.
            // Sert à l'assemblage, qui ne lit que la partie d'une tuile qui tombe dans un bloc de la mosaïque.
            static cv::Mat correctRegion(const cv::Mat& region, const RegionInTile& placement, double gain,
                                         double fieldStrength);

            // Gains, champ et bilan avant / A / B, sur plusieurs lignes (journal)
            static std::string buildReport(const EqualizationResult& result);

            // Échec de BrightnessEqualizerClass::estimate : aucune paire n'a d'échantillon exploitable
            static std::string describeNoSampleFailure(std::size_t pairCount);

    };

}
