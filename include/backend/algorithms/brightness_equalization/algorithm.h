// include/backend/algorithms/brightness_equalization/algorithm.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"

#include <opencv2/core.hpp>

#include <vector>

namespace BrightnessEqualizationNs {

    // Étape 2 : calculs sur les luminances réduites.
    //
    // Modèle (en logarithme), pour chaque pixel du recouvrement de la paire (A, B) :
    //     log I_A − log I_B = log g_A − log g_B + c · (r²_A − r²_B)
    // g : gain de la tuile ; c : force du champ d'éclairage exp(c · r²), commun à toutes les tuiles (méthode B ;
    // c = 0 pour la méthode A). r² = u² + v², u et v dans [−1, 1] : position normalisée du pixel dans sa tuile.
    class AlgorithmClass {

        public:
            // Un échantillon par pixel de A dont le correspondant dans B (B placée à pair.translation) est dans les
            // deux tuiles, hors de la marge, et ni trop sombre ni saturé dans les deux. Vide si les tuiles ne se
            // recouvrent pas.
            static PairSamples collectSamples(const ReducedTile& tileA, const ReducedTile& tileB, const NeighbourPair& pair,
                                              const Parameters& parameters);

            // Moindres carrés robustes (IRLS, poids de Huber) sur tous les échantillons à la fois.
            // Contrainte de jauge : moyenne des log g = 0 (conserve la luminosité globale de l'œuvre).
            // withField = false : méthode A (c = 0) ; true : méthode B.
            static Solution solveGains(const std::vector<PairSamples>& pairSamples, int tileCount, bool withField,
                                       const Parameters& parameters);

            // RMS et écart absolu médian (en %) de l'écart restant sur les échantillons, après correction par
            // `solution` (Solution vide : avant correction). Remplit residualRms et medianResidual.
            static void computeResiduals(const std::vector<PairSamples>& pairSamples, const Solution& solution,
                                         QualityReport& report);

            // Écart moyen et variation basse fréquence (en %) dans les recouvrements, sur les luminances réduites
            // corrigées par `solution` (Solution vide : avant correction). Remplit meanDifference et
            // lowFrequencyVariation.
            static void measureOverlaps(const std::vector<ReducedTile>& tiles, const std::vector<NeighbourPair>& pairs,
                                        const Solution& solution, QualityReport& report);

            // Luminance réduite corrigée : luminance / (gain · exp(fieldStrength · r²)), ramenée dans [0, 1]
            static cv::Mat correctReducedLuminance(const cv::Mat& luminance, double gain, double fieldStrength);

    };

}
