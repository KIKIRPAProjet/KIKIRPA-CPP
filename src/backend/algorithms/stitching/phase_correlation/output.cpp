// src/backend/algorithms/stitching/phase_correlation/output.cpp
#include "algorithms/stitching/phase_correlation/output.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace PhaseCorrelationNs {

    using DataNs::Direction;

    namespace {

        constexpr double SAFETY_MARGIN = 10.0;   // px ajoutés à la marge conseillée (pic diagnostiqué au pixel près)

        // Nombre écrit avec `decimalCount` chiffres après la virgule.
        std::string formatNumber(double value, int decimalCount) {
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.*f", decimalCount, value);
            return buffer;
        }

        // Tri par confiance décroissante, puis on garde au plus maximumPointCount points.
        void sortAndLimit(std::vector<Correspondence>& points, int maximumPointCount) {
            std::sort(points.begin(), points.end(),
                      [](const Correspondence& first, const Correspondence& second) { return first.confidence > second.confidence; });
            if (static_cast<int>(points.size()) > maximumPointCount) points.resize(std::max(0, maximumPointCount));
        }

        // (point dans A, point dans B), en coordonnées des tuiles entières.
        std::pair<cv::Point2f, cv::Point2f> toTileCoordinates(const Correspondence& correspondence, cv::Point originInA) {
            // Un pixel (X, Y) de A correspond au pixel (X, Y) - translation de B
            const cv::Point2f pointInA = correspondence.pointInA + cv::Point2f(originInA);
            const cv::Point2f pointInB = pointInA - cv::Point2f(static_cast<float>(correspondence.localTranslation.x),
                                                                static_cast<float>(correspondence.localTranslation.y));
            return {pointInA, pointInB};
        }

        // A et B avaient été échangées : on inverse la translation et on échange pointsInA / pointsInB.
        void undoOrientation(DataNs::Matches& matches) {
            matches.translationX = -matches.translationX;
            matches.translationY = -matches.translationY;
            std::swap(matches.pointsInA, matches.pointsInB);
        }

    }

    DataNs::Matches OutputClass::buildMatches(const OrientedPair& pair, const Measurement& measurement, cv::Point originInA,
                                              std::vector<Correspondence> points, int maximumPointCount) {
        DataNs::Matches matches;
        matches.translationX = static_cast<float>(measurement.translationX);
        matches.translationY = static_cast<float>(measurement.translationY);

        sortAndLimit(points, maximumPointCount);
        matches.pointsInA.reserve(points.size());
        matches.pointsInB.reserve(points.size());
        matches.confidenceScores.reserve(points.size());
        for (const Correspondence& correspondence : points) {
            const auto [pointInA, pointInB] = toTileCoordinates(correspondence, originInA);
            matches.pointsInA.push_back(pointInA);
            matches.pointsInB.push_back(pointInB);
            matches.confidenceScores.push_back(correspondence.confidence);
        }

        if (pair.isSwapped) undoOrientation(matches);
        return matches;
    }

    std::string OutputClass::describePairFailure(const cv::Mat& imageA, const cv::Mat& imageB,
                                                 const DataNs::PairContext& context) {
        if (imageA.empty() || imageB.empty())
            return std::string("tile ") + (imageA.empty() ? "A" : "B") + " is empty (image not loaded)";
        if (imageA.size() != imageB.size())
            return "tiles have different sizes (A " + std::to_string(imageA.cols) + " x " + std::to_string(imageA.rows) +
                   ", B " + std::to_string(imageB.cols) + " x " + std::to_string(imageB.rows) + ")";
        return "nominal overlap " + formatNumber(context.overlapFraction, 2) +
               " outside ]0, 1] (fraction of the tile, e.g. 0.2)";
    }

    std::string OutputClass::describeStripsFailure(const DataNs::PairContext& context, const Parameters& parameters) {
        return "not enough common content in the nominal overlap strips (" + formatNumber(context.overlapFraction * 100, 0) +
               " %): less than " + formatNumber(parameters.minimumValidFraction * 100, 0) + " % of usable pixels (black "
               "background, saturated reflections, uniform area) or common area too small. Also check the nominal overlap "
               "and the direction";
    }

    std::string OutputClass::describeGlobalFailure(const Diagnostic& diagnostic, const OrientedPair& pair, const Strips& strips,
                                                   const DataNs::PairContext& context, const Parameters& parameters) {
        // Écart au recouvrement nominal dans le sens du déplacement (il fixe le recouvrement réel) et en travers
        const bool isBottom = pair.direction == Direction::Bottom;
        const double alongShift = isBottom ? diagnostic.unconstrainedPeak.y : diagnostic.unconstrainedPeak.x;
        const double acrossShift = isBottom ? diagnostic.unconstrainedPeak.x : diagnostic.unconstrainedPeak.y;
        const std::string marginRatioText = "peak / second peak ratio " + formatNumber(diagnostic.peakRatioInMargin, 2) +
                                            " < threshold " + formatNumber(parameters.minimumPeakRatio, 2);

        if (!diagnostic.isOutsideMargin)
            return "ambiguous correlation peak within the rail margin (" + marginRatioText + "): weakly textured common "
                   "area or repetitive pattern";

        const std::string offsetText = "the best peak is " + formatNumber(std::abs(alongShift), 0) + " px from the nominal "
                                       "overlap along the motion and " + formatNumber(std::abs(acrossShift), 0) + " px across, "
                                       "beyond the rail margin (±" + formatNumber(strips.searchMargin, 0) + " px)";
        if (diagnostic.unconstrainedPeakRatio < parameters.minimumPeakRatio)
            return "ambiguous correlation peak (" + marginRatioText + "); " + offsetText + ", but it is ambiguous too "
                   "(ratio " + formatNumber(diagnostic.unconstrainedPeakRatio, 2) + "): weakly textured area, repetitive "
                   "pattern or very wrong nominal overlap";

        // Recouvrement réel estimé : la bande nominale commence à `origin` ; le pic la décale de `alongShift`
        const double tileLength = isBottom ? pair.imageA.rows : pair.imageA.cols;
        const double stripStart = isBottom ? strips.origin.y : strips.origin.x;
        const double actualOverlapFraction = (tileLength - stripStart - alongShift) / tileLength;
        return "translation outside the rail margin: " + offsetText + " (ratio " +
               formatNumber(diagnostic.unconstrainedPeakRatio, 2) + "). Increase the rail margin (Parameters::railMargin, "
               "'marge' in the step, >= " +
               formatNumber(std::ceil(std::max(std::abs(alongShift), std::abs(acrossShift)) + SAFETY_MARGIN), 0) +
               " px) or give the actual overlap (about " + formatNumber(actualOverlapFraction * 100, 0) + " % instead of " +
               formatNumber(context.overlapFraction * 100, 0) + " %). To check: on a repetitive pattern, this peak may be "
               "wrong";
    }

    std::string OutputClass::describeCommonAreaFailure(cv::Point2d translation, const Parameters& parameters) {
        return "not enough content in the actual common area, B placed at (" + formatNumber(translation.x, 0) + ", " +
               formatNumber(translation.y, 0) + ") in A: the tiles do not overlap, or less than " +
               formatNumber(parameters.minimumValidFraction * 100, 0) + " % of usable pixels, or common area too small. "
               "Check the first pass translation (PairContext::estimatedTranslation)";
    }

    std::string OutputClass::describeRefinementFailure(cv::Point2d translation, const Strips& commonArea) {
        return "second pass: the peak touches the border of its margin (±" + formatNumber(commonArea.searchMargin, 0) +
               " px around (" + formatNumber(translation.x, 0) + ", " + formatNumber(translation.y, 0) + ")). The first "
               "pass translation (PairContext::estimatedTranslation) is wrong by more than this margin";
    }

}
