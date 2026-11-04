// src/backend/algorithms/stitching/overlap_estimation/overlap_estimator.cpp
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"

#include "algorithms/stitching/phase_correlation/algorithm.h"
#include "algorithms/stitching/phase_correlation/output.h"
#include "algorithms/stitching/phase_correlation/preparation.h"

#include <algorithm>
#include <cmath>

namespace OverlapEstimationNs {

    using PhaseCorrelationNs::AlgorithmClass;
    using PhaseCorrelationNs::OutputClass;
    using PhaseCorrelationNs::PreparationClass;

    namespace {

        constexpr int PIXEL_PRECISION_FACTOR = 1;   // upsamplingFactor de la première passe : pic entier, sans sous-pixel

        // Translation et recouvrement réel dans le repère des tuiles d'origine, à partir de la mesure faite
        // sur la paire orientée (B en bas ou à droite).
        DataNs::OverlapEstimate toTileFrame(const PhaseCorrelationNs::OrientedPair& pair,
                                            const PhaseCorrelationNs::Measurement& measurement) {
            // Paire orientée : B est en bas ou à droite, son coin est à (tuile − recouvrement) dans le sens du déplacement
            const bool isBottom = pair.direction == DataNs::Direction::Bottom;
            const double tileLength = isBottom ? pair.imageA.rows : pair.imageA.cols;
            const double startOfB = isBottom ? measurement.translationY : measurement.translationX;

            DataNs::OverlapEstimate overlapEstimate;
            overlapEstimate.translationBInA = PreparationClass::orientTranslation(pair, {measurement.translationX,
                                                                                         measurement.translationY});
            overlapEstimate.overlapFraction = static_cast<float>((tileLength - startOfB) / tileLength);
            overlapEstimate.peakRatio = measurement.peakRatio;
            return overlapEstimate;
        }

    }

    OverlapEstimatorClass::OverlapEstimatorClass(const PhaseCorrelationNs::Parameters& initialParameters)
        : parameters(initialParameters) {
        PreparationClass::validateParameters(parameters);
        // Le pixel suffit pour découper la zone commune réelle ; le sous-pixel, le plus coûteux, n'est calculé
        // qu'une fois, sur cette zone (deuxième passe)
        parameters.upsamplingFactor = PIXEL_PRECISION_FACTOR;
    }

    std::optional<DataNs::OverlapEstimate> OverlapEstimatorClass::estimate(const cv::Mat& imageA, const cv::Mat& imageB,
                                                                           const DataNs::PairContext& context) {
        failureReason.clear();
        const auto pair = PreparationClass::orientPair(imageA, imageB, context);
        if (!pair) {
            failureReason = OutputClass::describePairFailure(imageA, imageB, context);
            return std::nullopt;
        }
        const auto strips = PreparationClass::extractNominalStrips(*pair, context.overlapFraction, parameters);
        if (!strips) {
            failureReason = OutputClass::describeStripsFailure(context, parameters);
            return std::nullopt;
        }
        const auto measurement = AlgorithmClass::measureGlobalTranslation(*strips, parameters);
        if (!measurement) {
            failureReason = OutputClass::describeGlobalFailure(AlgorithmClass::diagnoseGlobalTranslation(*strips, parameters),
                                                               *pair, *strips, context, parameters);
            return std::nullopt;
        }
        return toTileFrame(*pair, *measurement);
    }

    std::pair<cv::Rect, cv::Rect> OverlapEstimatorClass::computeCommonAreas(cv::Size sizeA, cv::Size sizeB,
                                                                            cv::Point2d translation) {
        // Arrondi au pixel : les deux rectangles ont alors exactement la même taille, et ce qui reste du décalage
        // (moins d'un demi-pixel, plus l'erreur de la première passe) est mesuré par la méthode d'appariement
        const cv::Point roundedTranslation(static_cast<int>(std::lround(translation.x)),
                                           static_cast<int>(std::lround(translation.y)));
        const int left = std::max(0, roundedTranslation.x), right = std::min(sizeA.width, roundedTranslation.x + sizeB.width);
        const int top = std::max(0, roundedTranslation.y), bottom = std::min(sizeA.height, roundedTranslation.y + sizeB.height);
        if (right <= left || bottom <= top) return {};
        const cv::Rect areaA(left, top, right - left, bottom - top);
        return {areaA, areaA - roundedTranslation};
    }

}
