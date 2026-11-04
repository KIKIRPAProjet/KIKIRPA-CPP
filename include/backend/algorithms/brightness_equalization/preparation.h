// include/backend/algorithms/brightness_equalization/preparation.h
#pragma once

#include "algorithms/brightness_equalization/brightness_equalization_types.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace BrightnessEqualizationNs {

    // Étape 1 : des tuiles brutes (8 ou 16 bits, gris ou couleur) aux luminances réduites de l'estimation.
    // Les tuiles ne sont jamais modifiées.
    class PreparationClass {

        public:
            // Lève std::invalid_argument si un paramètre est hors de son domaine
            static void validateParameters(const Parameters& parameters);

            // Tuile en pleine résolution → luminance réduite au 1/parameters.reductionFactor et floutée. Tuile vide :
            // ReducedTile vide.
            static ReducedTile reduceTile(const cv::Mat& tile, const Parameters& parameters);

            // Même chose à partir d'une version déjà réduite (ex. niveau 1/8 de la pyramide de TileClass) : seul le
            // flou est appliqué. `fullSize` est la taille de la tuile en pleine résolution ; l'échelle réelle en est
            // déduite.
            static ReducedTile prepareReducedTile(const cv::Mat& reducedImage, cv::Size fullSize, const Parameters& parameters);

            // Message d'erreur si les entrées de l'estimation sont inexploitables, vide sinon
            // (moins de 2 tuiles, tuile vide, indice de paire hors limites, paire d'une tuile avec elle-même)
            static std::string validateInputs(const std::vector<ReducedTile>& tiles, const std::vector<NeighbourPair>& pairs);

            // Luminance dans [0, 1], CV_32F : 8 bits / 255, 16 bits / 65535, flottant supposé déjà dans [0, 1].
            // Couleur (BGR ou BGRA, lue par cv::imread) : luminance pondérée, canal alpha ignoré.
            static cv::Mat computeLuminance(const cv::Mat& image);

            // Plus grande valeur d'un pixel pour la profondeur OpenCV `depth` (255, 65535… ; 1 pour un flottant)
            static double maximumValueOfDepth(int depth);

    };

}
