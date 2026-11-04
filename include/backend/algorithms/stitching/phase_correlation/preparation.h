// include/backend/algorithms/stitching/phase_correlation/preparation.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <optional>

namespace PhaseCorrelationNs {

    // Étape 1 : des tuiles brutes (8 ou 16 bits, gris ou couleur) aux images de travail
    // (gris float64, NaN = pixel vide). Aucune conversion en 8 bits, les tuiles ne sont jamais modifiées.
    class PreparationClass {

        public:
            // Lève std::invalid_argument si un paramètre est hors de son domaine (patchSize < 8, valeur négative…).
            static void validateParameters(const Parameters& parameters);

            // Vérifie la paire, la ramène au cas « B en bas / à droite » (échange A et B si besoin)
            // et calcule les zones extérieures. Vide si les tuiles sont vides, de tailles différentes,
            // ou si le recouvrement n'est pas dans ]0, 1].
            static std::optional<OrientedPair> orientPair(const cv::Mat& imageA, const cv::Mat& imageB,
                                                          const DataNs::PairContext& context);

            // Translation (coin de B dans A) passée du repère des tuiles d'origine à celui de la paire orientée, ou
            // l'inverse : opposée si A et B ont été échangées, inchangée sinon.
            static cv::Point2d orientTranslation(const OrientedPair& pair, cv::Point2d translation);

            // Première passe : bandes censées se recouvrir (recouvrement nominal), en gris, découpées sur la zone où
            // les deux ont du contenu, avec la marge du rail. Vide si pas assez de contenu commun
            // (moins de parameters.minimumValidFraction pixels exploitables dans les deux bandes, ou zone trop petite).
            static std::optional<Strips> extractNominalStrips(const OrientedPair& pair, double overlapFraction,
                                                              const Parameters& parameters);

            // Deuxième passe : zone commune réelle, B placée à `translation` (première passe, repère de la paire
            // orientée), en gris, découpée sur la zone avec contenu, avec une marge de SECOND_PASS_MARGIN px.
            // Vide dans les mêmes cas que extractNominalStrips(), ou si les tuiles ne se superposent pas à cette
            // translation.
            static std::optional<Strips> extractCommonArea(const OrientedPair& pair, cv::Point2d translation,
                                                           const Parameters& parameters);

            // Zone commune réelle une fois B placée à la translation globale. Vide si elle est trop étroite
            // pour y placer des patchs de parameters.patchSize px (avec la marge de correction
            // parameters.maximumLocalDeviation).
            static std::optional<Overlap> extractOverlap(const OrientedPair& pair, const Measurement& measurement,
                                                         const Parameters& parameters);

            // Masque 8 bits des pixels non NaN.
            static cv::Mat computeValidMask(const cv::Mat& image);

            // Copie où les pixels NaN sont remplacés par la moyenne des pixels valides (valeur neutre).
            static cv::Mat fillEmptyPixels(const cv::Mat& image);

    };

}
