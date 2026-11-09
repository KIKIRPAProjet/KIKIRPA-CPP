// src/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.cpp
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"

#include "algorithms/stitching/phase_correlation/algorithm.h"
#include "algorithms/stitching/phase_correlation/output.h"
#include "algorithms/stitching/phase_correlation/preparation.h"

#include <optional>
#include <utility>
#include <vector>

namespace PhaseCorrelationNs {

    PhaseCorrelationMatcherClass::PhaseCorrelationMatcherClass(const Parameters& initialParameters)
        : parameters(initialParameters), overlapEstimator(initialParameters) {
        PreparationClass::validateParameters(parameters);
    }

    bool PhaseCorrelationMatcherClass::match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                                             DataNs::Matches& matches) {
        matches = {};
        failureReason.clear();

        // 1. Première passe : position de B au pixel près sur les bandes nominales, sauf si le pipeline l'a faite
        std::optional<cv::Point2d> translation = context.estimatedTranslation;
        if (!translation) {
            const auto overlapEstimate = overlapEstimator.estimate(imageA, imageB, context);
            if (!overlapEstimate) {
                failureReason = overlapEstimator.getFailureReason();
                return false;
            }
            translation = overlapEstimate->translationBInA;
        }

        // 2. Deuxième passe : translation au sous-pixel sur la zone commune réelle (en cas d'échec : message dans
        // failureReason)
        const auto pair = PreparationClass::orientPair(imageA, imageB, context);
        if (!pair) {
            failureReason = OutputClass::describePairFailure(imageA, imageB, context);
            return false;
        }
        const auto commonArea = PreparationClass::extractCommonArea(*pair, PreparationClass::orientTranslation(*pair, *translation),
                                                                    parameters);
        if (!commonArea) {
            failureReason = OutputClass::describeCommonAreaFailure(*translation, parameters);
            return false;
        }
        const auto measurement = AlgorithmClass::refineGlobalTranslation(*commonArea, parameters);
        if (!measurement) {
            failureReason = OutputClass::describeRefinementFailure(*translation, *commonArea);
            return false;
        }

        // 3. Points raffinés localement dans la zone commune réelle (absents si elle est trop étroite)
        const auto overlap = PreparationClass::extractOverlap(*pair, *measurement, parameters);
        std::vector<Correspondence> points;
        if (overlap && parameters.maximumPointCount > 0)
            points = AlgorithmClass::measureLocalPoints(*overlap, *measurement, parameters);

        // 4. Mise en forme dans le repère des tuiles d'origine
        matches = OutputClass::buildMatches(*pair, *measurement, overlap ? overlap->originInA : cv::Point(), std::move(points),
                                            parameters.maximumPointCount);
        return true;
    }

}
