// src/backend/algorithms/brightness_equalization/algorithm.cpp
#include "algorithms/brightness_equalization/algorithm.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace BrightnessEqualizationNs {

    namespace {

        constexpr double MAD_TO_STANDARD_DEVIATION = 1.4826;   // MAD → écart-type pour une loi normale
        constexpr double MINIMUM_RESIDUAL = 1e-9;              // évite la division par 0 dans les poids de Huber
        constexpr float MEASUREMENT_LOW_THRESHOLD = 0.02f;     // measureOverlaps() : pixels exclus en dessous / au-dessus
        constexpr float MEASUREMENT_HIGH_THRESHOLD = 0.98f;
        constexpr int MEASUREMENT_EROSION_SIZE = 5;            // px (image réduite) : bord des zones valides retiré
        constexpr int LOW_FREQUENCY_KERNEL_SIZE = 51;          // px : flou de la variation basse fréquence
        constexpr double LOW_FREQUENCY_SIGMA = 12.0;
        constexpr double LOW_FREQUENCY_MINIMUM_WEIGHT = 0.3;   // part minimale de pixels valides sous le noyau

        // r² d'un point (x, y) en pleine résolution, dans une tuile de taille `tileSize`
        double squaredRadius(double x, double y, cv::Size tileSize) {
            const double normalizedX = 2.0 * x / tileSize.width - 1.0;
            const double normalizedY = 2.0 * y / tileSize.height - 1.0;
            return normalizedX * normalizedX + normalizedY * normalizedY;
        }

        // Valeur au centile `percentile` (0 à 100) ; `values` est réordonné
        double computePercentile(std::vector<double>& values, double percentile) {
            if (values.empty()) return 0.0;
            // Interpolation linéaire entre les deux rangs voisins, comme np.percentile
            const double rank = percentile / 100.0 * (values.size() - 1);
            const std::size_t lowerIndex = static_cast<std::size_t>(std::floor(rank));
            const std::size_t upperIndex = std::min(lowerIndex + 1, values.size() - 1);
            std::nth_element(values.begin(), values.begin() + lowerIndex, values.end());
            const double lowerValue = values[lowerIndex];
            if (upperIndex == lowerIndex) return lowerValue;
            const double upperValue = *std::min_element(values.begin() + lowerIndex + 1, values.end());
            return lowerValue + (rank - lowerIndex) * (upperValue - lowerValue);
        }

    }

    PairSamples AlgorithmClass::collectSamples(const ReducedTile& tileA, const ReducedTile& tileB, const NeighbourPair& pair,
                                               const Parameters& parameters) {
        PairSamples samples;
        samples.tileIndexA = pair.tileIndexA;
        samples.tileIndexB = pair.tileIndexB;
        const cv::Mat& luminanceA = tileA.luminance;
        const cv::Mat& luminanceB = tileB.luminance;
        // Échelle réelle de chaque image réduite (l'arrondi de la taille la rend un peu différente de
        // parameters.reductionFactor)
        const double scaleXA = tileA.fullSize.width / static_cast<double>(luminanceA.cols);
        const double scaleYA = tileA.fullSize.height / static_cast<double>(luminanceA.rows);
        const double scaleXB = tileB.fullSize.width / static_cast<double>(luminanceB.cols);
        const double scaleYB = tileB.fullSize.height / static_cast<double>(luminanceB.rows);
        const double margin = parameters.borderMargin;
        const float lowThreshold = static_cast<float>(parameters.lowThreshold);
        const float highThreshold = static_cast<float>(parameters.highThreshold);

        for (int row = 0; row < luminanceA.rows; ++row) {
            // Centre du pixel réduit, en pleine résolution dans A, puis le même point dans B
            const double positionYInA = (row + 0.5) * scaleYA;
            const double positionYInB = positionYInA - pair.translation.y;
            if (positionYInA <= margin || positionYInA >= tileA.fullSize.height - margin || positionYInB <= margin ||
                positionYInB >= tileB.fullSize.height - margin)
                continue;
            const double reducedRowB = positionYInB / scaleYB - 0.5;   // ligne (fractionnaire) dans l'image réduite de B
            const int upperRowB = static_cast<int>(std::floor(reducedRowB));
            if (upperRowB < 0 || upperRowB + 1 >= luminanceB.rows) continue;
            const float fractionY = static_cast<float>(reducedRowB - upperRowB);
            const float* rowValuesA = luminanceA.ptr<float>(row);
            const float* upperRowValuesB = luminanceB.ptr<float>(upperRowB);
            const float* lowerRowValuesB = luminanceB.ptr<float>(upperRowB + 1);

            for (int column = 0; column < luminanceA.cols; ++column) {
                const float intensityA = rowValuesA[column];
                if (intensityA <= lowThreshold || intensityA >= highThreshold) continue;   // trop sombre ou saturé (reflet)
                const double positionXInA = (column + 0.5) * scaleXA;
                const double positionXInB = positionXInA - pair.translation.x;
                if (positionXInA <= margin || positionXInA >= tileA.fullSize.width - margin || positionXInB <= margin ||
                    positionXInB >= tileB.fullSize.width - margin)
                    continue;
                const double reducedColumnB = positionXInB / scaleXB - 0.5;
                const int leftColumnB = static_cast<int>(std::floor(reducedColumnB));
                if (leftColumnB < 0 || leftColumnB + 1 >= luminanceB.cols) continue;
                const float fractionX = static_cast<float>(reducedColumnB - leftColumnB);
                // Interpolation bilinéaire dans B (comme cv::remap INTER_LINEAR dans le notebook)
                const float intensityB =
                    (1 - fractionY) * ((1 - fractionX) * upperRowValuesB[leftColumnB] + fractionX * upperRowValuesB[leftColumnB + 1]) +
                    fractionY * ((1 - fractionX) * lowerRowValuesB[leftColumnB] + fractionX * lowerRowValuesB[leftColumnB + 1]);
                if (intensityB <= lowThreshold || intensityB >= highThreshold) continue;
                samples.squaredRadiiA.push_back(static_cast<float>(squaredRadius(positionXInA, positionYInA, tileA.fullSize)));
                samples.squaredRadiiB.push_back(static_cast<float>(squaredRadius(positionXInB, positionYInB, tileB.fullSize)));
                samples.logDifferences.push_back(std::log(intensityA) - std::log(intensityB));
            }
        }
        return samples;
    }

    Solution AlgorithmClass::solveGains(const std::vector<PairSamples>& pairSamples, int tileCount, bool withField,
                                        const Parameters& parameters) {
        const int unknownCount = tileCount + (withField ? 1 : 0);   // inconnues : log g_0 … log g_(n−1) [, c]
        std::size_t totalSampleCount = 0;
        std::vector<bool> isLinked(tileCount, false);
        for (const auto& samples : pairSamples) {
            totalSampleCount += samples.logDifferences.size();
            if (!samples.logDifferences.empty()) isLinked[samples.tileIndexA] = isLinked[samples.tileIndexB] = true;
        }

        // Chaque échantillon est une équation  log g_a − log g_b + c · (r²_a − r²_b) = d.
        // On résout les équations normales (m × m) au lieu du système complet (millions de lignes) : chaque ligne
        // n'a que 2 ou 3 coefficients non nuls, on les accumule directement.
        std::vector<double> weights(totalSampleCount, 1.0);   // poids IRLS (carré du poids de ligne du notebook)
        std::vector<double> absoluteResiduals(totalSampleCount);
        Solution solution;
        solution.logGains.assign(tileCount, 0.0);

        for (int iterationIndex = 0; iterationIndex < parameters.iterationCount; ++iterationIndex) {
            cv::Mat normalMatrix = cv::Mat::zeros(unknownCount, unknownCount, CV_64F);
            cv::Mat rightHandSide = cv::Mat::zeros(unknownCount, 1, CV_64F);
            double* normalValues = normalMatrix.ptr<double>();
            double* rightHandSideValues = rightHandSide.ptr<double>();
            std::size_t sampleIndex = 0;
            for (const auto& samples : pairSamples) {
                const int tileIndexA = samples.tileIndexA, tileIndexB = samples.tileIndexB;
                for (std::size_t pairSampleIndex = 0; pairSampleIndex < samples.logDifferences.size();
                     ++pairSampleIndex, ++sampleIndex) {
                    const double weight = weights[sampleIndex], logDifference = samples.logDifferences[pairSampleIndex];
                    normalValues[tileIndexA * unknownCount + tileIndexA] += weight;
                    normalValues[tileIndexB * unknownCount + tileIndexB] += weight;
                    normalValues[tileIndexA * unknownCount + tileIndexB] -= weight;
                    normalValues[tileIndexB * unknownCount + tileIndexA] -= weight;
                    rightHandSideValues[tileIndexA] += weight * logDifference;
                    rightHandSideValues[tileIndexB] -= weight * logDifference;
                    if (withField) {
                        const double squaredRadiusDifference = static_cast<double>(samples.squaredRadiiA[pairSampleIndex]) -
                                                               samples.squaredRadiiB[pairSampleIndex];
                        normalValues[tileIndexA * unknownCount + tileCount] += weight * squaredRadiusDifference;
                        normalValues[tileCount * unknownCount + tileIndexA] += weight * squaredRadiusDifference;
                        normalValues[tileIndexB * unknownCount + tileCount] -= weight * squaredRadiusDifference;
                        normalValues[tileCount * unknownCount + tileIndexB] -= weight * squaredRadiusDifference;
                        normalValues[tileCount * unknownCount + tileCount] +=
                            weight * squaredRadiusDifference * squaredRadiusDifference;
                        rightHandSideValues[tileCount] += weight * squaredRadiusDifference * logDifference;
                    }
                }
            }
            // Contrainte de jauge : gaugeWeight · (log g_0 + … + log g_(n−1)) = 0. Sans elle, tous les gains
            // pourraient être multipliés par une même constante.
            const double squaredGaugeWeight = parameters.gaugeWeight * parameters.gaugeWeight;
            for (int rowIndex = 0; rowIndex < tileCount; ++rowIndex)
                for (int columnIndex = 0; columnIndex < tileCount; ++columnIndex)
                    normalValues[rowIndex * unknownCount + columnIndex] += squaredGaugeWeight;
            // Tuile sans aucun échantillon : log g = 0 imposé (gain 1, tuile non égalisée), sinon indéterminé
            for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
                if (!isLinked[tileIndex]) normalValues[tileIndex * unknownCount + tileIndex] += squaredGaugeWeight;

            cv::Mat solutionVector;
            cv::solve(normalMatrix, rightHandSide, solutionVector, cv::DECOMP_SVD);
            for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
                solution.logGains[tileIndex] = solutionVector.at<double>(tileIndex);
            solution.fieldStrength = withField ? solutionVector.at<double>(tileCount) : 0.0;

            // Résidus et nouveaux poids de Huber : les grands écarts (reflets, erreurs de recalage) pèsent moins
            sampleIndex = 0;
            for (const auto& samples : pairSamples)
                for (std::size_t pairSampleIndex = 0; pairSampleIndex < samples.logDifferences.size();
                     ++pairSampleIndex, ++sampleIndex) {
                    const double squaredRadiusDifference = static_cast<double>(samples.squaredRadiiA[pairSampleIndex]) -
                                                           samples.squaredRadiiB[pairSampleIndex];
                    absoluteResiduals[sampleIndex] = std::abs(
                        samples.logDifferences[pairSampleIndex] -
                        (solution.logGains[samples.tileIndexA] - solution.logGains[samples.tileIndexB] +
                         solution.fieldStrength * squaredRadiusDifference));
                }
            std::vector<double> sortedResiduals(absoluteResiduals);
            const double robustScale = computePercentile(sortedResiduals, 50.0) * MAD_TO_STANDARD_DEVIATION;
            if (robustScale <= 0) break;   // données exactes : rien à repondérer
            for (std::size_t index = 0; index < totalSampleCount; ++index)
                weights[index] = std::min(1.0, parameters.huberThreshold * robustScale /
                                                   std::max(absoluteResiduals[index], MINIMUM_RESIDUAL));
        }
        return solution;
    }

    void AlgorithmClass::computeResiduals(const std::vector<PairSamples>& pairSamples, const Solution& solution,
                                          QualityReport& report) {
        std::vector<double> absolutePercentages;
        double sumOfSquares = 0.0;
        for (const auto& samples : pairSamples)
            for (std::size_t index = 0; index < samples.logDifferences.size(); ++index) {
                double residual = samples.logDifferences[index];
                if (!solution.logGains.empty())
                    residual -= solution.logGains[samples.tileIndexA] - solution.logGains[samples.tileIndexB] +
                                solution.fieldStrength *
                                    (static_cast<double>(samples.squaredRadiiA[index]) - samples.squaredRadiiB[index]);
                const double percentage = 100.0 * (std::exp(residual) - 1.0);   // écart de luminosité restant, en %
                sumOfSquares += percentage * percentage;
                absolutePercentages.push_back(std::abs(percentage));
            }
        if (absolutePercentages.empty()) return;
        report.residualRms = std::sqrt(sumOfSquares / absolutePercentages.size());
        report.medianResidual = computePercentile(absolutePercentages, 50.0);
    }

    void AlgorithmClass::measureOverlaps(const std::vector<ReducedTile>& tiles, const std::vector<NeighbourPair>& pairs,
                                         const Solution& solution, QualityReport& report) {
        std::vector<cv::Mat> luminances(tiles.size());
        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
            luminances[tileIndex] = solution.logGains.empty()
                                        ? tiles[tileIndex].luminance
                                        : correctReducedLuminance(tiles[tileIndex].luminance,
                                                                  std::exp(solution.logGains[tileIndex]),
                                                                  solution.fieldStrength);

        const cv::Mat erosionKernel = cv::Mat::ones(MEASUREMENT_EROSION_SIZE, MEASUREMENT_EROSION_SIZE, CV_8U);
        double differenceSum = 0.0, rangeSum = 0.0;
        int differenceCount = 0, rangeCount = 0;
        for (const auto& pair : pairs) {
            const cv::Mat& luminanceA = luminances[pair.tileIndexA];
            const cv::Mat& luminanceB = luminances[pair.tileIndexB];
            const double scaleXA = tiles[pair.tileIndexA].fullSize.width / static_cast<double>(luminanceA.cols);
            const double scaleYA = tiles[pair.tileIndexA].fullSize.height / static_cast<double>(luminanceA.rows);
            const double scaleXB = tiles[pair.tileIndexB].fullSize.width / static_cast<double>(luminanceB.cols);
            const double scaleYB = tiles[pair.tileIndexB].fullSize.height / static_cast<double>(luminanceB.rows);
            // B dans le repère réduit de A (pixels hors de B : −1, exclus ensuite)
            const cv::Mat affineTransform = (cv::Mat_<double>(2, 3) << scaleXB / scaleXA, 0, pair.translation.x / scaleXA,
                                                                       0, scaleYB / scaleYA, pair.translation.y / scaleYA);
            cv::Mat luminanceBInA;
            cv::warpAffine(luminanceB, luminanceBInA, affineTransform, luminanceA.size(), cv::INTER_LINEAR,
                           cv::BORDER_CONSTANT, cv::Scalar(-1));

            cv::Mat validMask = (luminanceBInA > MEASUREMENT_LOW_THRESHOLD) & (luminanceBInA < MEASUREMENT_HIGH_THRESHOLD) &
                                (luminanceA > MEASUREMENT_LOW_THRESHOLD) & (luminanceA < MEASUREMENT_HIGH_THRESHOLD);
            cv::erode(validMask, validMask, erosionKernel);
            if (cv::countNonZero(validMask) == 0) continue;

            // Écart moyen : différence absolue moyenne entre les deux tuiles, en % de la dynamique
            cv::Mat absoluteDifference;
            cv::absdiff(luminanceA, luminanceBInA, absoluteDifference);
            differenceSum += 100.0 * cv::mean(absoluteDifference, validMask)[0];
            ++differenceCount;

            // Variation basse fréquence : rapport log lissé sur une grande échelle, écart entre 5e et 95e centiles
            cv::Mat logA, logB, logRatio;
            cv::log(cv::max(luminanceA, 1e-6), logA);
            cv::log(cv::max(luminanceBInA, 1e-6), logB);
            logRatio = logA - logB;
            logRatio.setTo(0, ~validMask);
            cv::Mat validWeights, smoothedNumerator, smoothedDenominator;
            validMask.convertTo(validWeights, CV_32F, 1.0 / 255);
            cv::GaussianBlur(logRatio, smoothedNumerator, cv::Size(LOW_FREQUENCY_KERNEL_SIZE, LOW_FREQUENCY_KERNEL_SIZE),
                             LOW_FREQUENCY_SIGMA);
            cv::GaussianBlur(validWeights, smoothedDenominator,
                             cv::Size(LOW_FREQUENCY_KERNEL_SIZE, LOW_FREQUENCY_KERNEL_SIZE), LOW_FREQUENCY_SIGMA);
            std::vector<double> smoothedRatios;
            for (int row = 0; row < validMask.rows; ++row) {
                const uchar* validRow = validMask.ptr<uchar>(row);
                const float* numeratorRow = smoothedNumerator.ptr<float>(row);
                const float* denominatorRow = smoothedDenominator.ptr<float>(row);
                for (int column = 0; column < validMask.cols; ++column)
                    if (validRow[column] && denominatorRow[column] > LOW_FREQUENCY_MINIMUM_WEIGHT)
                        smoothedRatios.push_back(numeratorRow[column] / std::max(denominatorRow[column], 1e-6f));
            }
            if (!smoothedRatios.empty()) {
                const double percentile5 = computePercentile(smoothedRatios, 5.0);
                const double percentile95 = computePercentile(smoothedRatios, 95.0);
                rangeSum += 100.0 * (std::exp(percentile95) - std::exp(percentile5));
            }
            ++rangeCount;
        }
        report.meanDifference = differenceCount ? differenceSum / differenceCount : 0.0;
        report.lowFrequencyVariation = rangeCount ? rangeSum / rangeCount : 0.0;
    }

    cv::Mat AlgorithmClass::correctReducedLuminance(const cv::Mat& luminance, double gain, double fieldStrength) {
        cv::Mat corrected(luminance.size(), CV_32F);
        const int width = luminance.cols, height = luminance.rows;
        std::vector<double> squaredNormalizedX(width);
        for (int column = 0; column < width; ++column) {
            const double normalizedX = 2.0 * (column + 0.5) / width - 1.0;
            squaredNormalizedX[column] = normalizedX * normalizedX;
        }
        for (int row = 0; row < height; ++row) {
            const double normalizedY = 2.0 * (row + 0.5) / height - 1.0;
            const float* sourceRow = luminance.ptr<float>(row);
            float* correctedRow = corrected.ptr<float>(row);
            for (int column = 0; column < width; ++column)
                correctedRow[column] = static_cast<float>(std::clamp(
                    sourceRow[column] * std::exp(-fieldStrength * (squaredNormalizedX[column] + normalizedY * normalizedY)) / gain,
                    0.0, 1.0));
        }
        return corrected;
    }

}
