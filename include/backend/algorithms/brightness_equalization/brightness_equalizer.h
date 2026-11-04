// include/backend/algorithms/brightness_equalization/brightness_equalizer.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"

#include <opencv2/core.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BrightnessEqualizationNs {

    // Égalisation de luminosité entre tuiles, avant la seamline (port du notebook egalisation_gain_champ_final.ipynb).
    // Seule la différence entre tuiles est corrigée, jamais l'œuvre elle-même :
    //   A : un gain par tuile ;
    //   B : A + un assombrissement vers les bords exp(c · r²), commun à toutes les tuiles.
    // Seul header à inclure depuis le pipeline ; le détail est dans brightness_equalization/ :
    //   PreparationClass (luminances réduites) → AlgorithmClass (échantillons, moindres carrés, bilan) →
    //   OutputClass (correction).
    //
    // Utilisation, une tuile en mémoire à la fois :
    //   BrightnessEqualizerClass equalizer;
    //   std::vector<ReducedTile> reducedTiles;
    //   for (path : tiles) reducedTiles.push_back(equalizer.reduceTile(cv::imread(path, cv::IMREAD_UNCHANGED)));
    //   auto result = equalizer.estimate(reducedTiles, pairs);   // pairs : translations du stitching
    //   if (!result) → equalizer.getFailureReason()
    //   for (i, path : tiles) corrected = BrightnessEqualizerClass::correctTile(cv::imread(path, …), *result, i,
    //                                                                           CorrectionMethod::GainAndField);
    //
    // Paramètres : structure Parameters (brightness_equalization_types.h). Chaque champ a une valeur par défaut ;
    // std::invalid_argument à la construction si une valeur est invalide.
    //   reductionFactor  8     réduction pour l'estimation (1/8)
    //   blurSigma        1.5   sigma (px réduits) du flou avant l'estimation
    //   borderMargin     40    px ignorés au bord de chaque tuile (pleine résolution)
    //   lowThreshold     0.03  pixels plus sombres exclus
    //   highThreshold    0.97  pixels plus clairs exclus (saturation)
    //   iterationCount   6     itérations IRLS
    //   huberThreshold   1.5   seuil de Huber (écarts-types robustes)
    //   gaugeWeight      1000  poids de la contrainte « moyenne des log g = 0 »
    class BrightnessEqualizerClass {

        public:
            // Sans argument : toutes les valeurs par défaut
            explicit BrightnessEqualizerClass(const Parameters& initialParameters = {});

            // 1. Luminance réduite d'une tuile en pleine résolution (8/16 bits, gris ou couleur)
            ReducedTile reduceTile(const cv::Mat& tile) const;

            // 1 bis. Même chose à partir d'une version déjà réduite (niveau de pyramide) et de la taille pleine
            // résolution
            ReducedTile reduceTile(const cv::Mat& reducedImage, cv::Size fullSize) const;

            // 2. Gains A, gains + champ B et bilan de qualité. Vide si l'estimation est impossible
            // (entrées invalides, aucune paire avec des échantillons) : la raison est dans getFailureReason().
            std::optional<EqualizationResult> estimate(const std::vector<ReducedTile>& tiles,
                                                       const std::vector<NeighbourPair>& pairs);

            // Pourquoi le dernier estimate() a échoué. Vide s'il a réussi.
            const std::string& getFailureReason() const { return failureReason; }

            // 3. Tuile corrigée en pleine résolution (voir OutputClass::correctTile pour le type de sortie)
            static cv::Mat correctTile(const cv::Mat& tile, const EqualizationResult& result, int tileIndex,
                                       CorrectionMethod method);

            // Paires à partir de positions globales (coin haut-gauche de chaque tuile, ex. TilePositionData::position) :
            // translation = position[b] − position[a]
            static std::vector<NeighbourPair> buildPairsFromPositions(const std::vector<cv::Point2d>& positions,
                                                                      const std::vector<std::pair<int, int>>& neighbours);

        private:
            Parameters parameters;       // vérifiés une seule fois, dans le constructeur
            std::string failureReason;   // message du dernier échec de estimate()

    };

}
