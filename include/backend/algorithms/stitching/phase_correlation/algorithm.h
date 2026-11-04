// include/backend/algorithms/stitching/phase_correlation/algorithm.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"

#include <optional>
#include <vector>

namespace PhaseCorrelationNs {

    // Étape 2 : calculs sur des images déjà préparées (gris float64, NaN = vide).
    class AlgorithmClass {

        public:
            // Première passe : translation globale par corrélation de phase sur les bandes. Vide si le pic est ambigu.
            static std::optional<Measurement> measureGlobalTranslation(const Strips& strips, const Parameters& parameters);

            // Deuxième passe : translation globale au 1/parameters.upsamplingFactor px sur la zone commune réelle
            // (PreparationClass::extractCommonArea). Pas de contrôle du second pic (fait par la première passe). Vide si
            // le pic entier est sur le bord de la marge : la translation de la première passe était fausse de plus que
            // la marge.
            static std::optional<Measurement> refineGlobalTranslation(const Strips& commonArea, const Parameters& parameters);

            // À appeler quand measureGlobalTranslation a échoué : meilleur pic cherché sur toute la surface (sans la
            // marge du rail), pour savoir si la translation réelle est simplement hors de la marge. Précision au pixel.
            static Diagnostic diagnoseGlobalTranslation(const Strips& strips, const Parameters& parameters);

            // Points texturés de la zone commune, re-mesurés chacun par une corrélation de phase locale.
            static std::vector<Correspondence> measureLocalPoints(const Overlap& overlap, const Measurement& globalMeasurement,
                                                                  const Parameters& parameters);

    };

}
