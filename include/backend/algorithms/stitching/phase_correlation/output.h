// include/backend/algorithms/stitching/phase_correlation/output.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace PhaseCorrelationNs {

    // Étape 3 : mise en forme du résultat au format commun des FeatureMatcherClass (DataNs::Matches),
    // ou du message qui explique pourquoi match() a renvoyé false.
    class OutputClass {

        public:
            // Translation + maximumPointCount meilleurs points, dans le repère des tuiles d'origine
            // (orientation demandée par le pipeline rétablie). originInA : coin de la zone commune dans A.
            static DataNs::Matches buildMatches(const OrientedPair& pair, const Measurement& measurement, cv::Point originInA,
                                                std::vector<Correspondence> points, int maximumPointCount);

            // Échec de PreparationClass::orientPair : tuile vide, tailles différentes ou recouvrement hors de ]0, 1].
            static std::string describePairFailure(const cv::Mat& imageA, const cv::Mat& imageB,
                                                   const DataNs::PairContext& context);

            // Échec de PreparationClass::extractNominalStrips : pas assez de contenu commun dans les bandes de
            // recouvrement nominal.
            static std::string describeStripsFailure(const DataNs::PairContext& context, const Parameters& parameters);

            // Échec de AlgorithmClass::measureGlobalTranslation : pic hors de la marge du rail (avec la marge et le
            // recouvrement à essayer), ou pic ambigu.
            static std::string describeGlobalFailure(const Diagnostic& diagnostic, const OrientedPair& pair, const Strips& strips,
                                                     const DataNs::PairContext& context, const Parameters& parameters);

            // Échec de PreparationClass::extractCommonArea : pas de zone commune exploitable à la translation de la
            // première passe (coin de B dans A, repère des tuiles d'origine).
            static std::string describeCommonAreaFailure(cv::Point2d translation, const Parameters& parameters);

            // Échec de AlgorithmClass::refineGlobalTranslation : le pic de la deuxième passe touche le bord de sa marge.
            static std::string describeRefinementFailure(cv::Point2d translation, const Strips& commonArea);

    };

}
