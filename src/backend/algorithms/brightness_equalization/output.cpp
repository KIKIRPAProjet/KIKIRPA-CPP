// src/backend/algorithms/brightness_equalization/output.cpp
#include "algorithms/brightness_equalization/output.h"

#include "algorithms/brightness_equalization/preparation.h"

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace BrightnessEqualizationNs {

    namespace {

        // Nombre écrit avec `decimalCount` chiffres après la virgule
        std::string formatNumber(double value, int decimalCount) {
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.*f", decimalCount, value);
            return buffer;
        }

        // Gains séparés par des espaces, 4 décimales
        std::string formatGains(const std::vector<double>& gains) {
            std::string text;
            for (std::size_t gainIndex = 0; gainIndex < gains.size(); ++gainIndex)
                text += (gainIndex ? " " : "") + formatNumber(gains[gainIndex], 4);
            return text;
        }

        // Une ligne du bilan : écart moyen, variation basse fréquence et résidus
        std::string formatQualityLine(const char* label, const QualityReport& qualityReport) {
            return std::string("  ") + label + " mean difference " + formatNumber(qualityReport.meanDifference, 2) +
                   " %, low-frequency variation " + formatNumber(qualityReport.lowFrequencyVariation, 2) +
                   " %, residual RMS " + formatNumber(qualityReport.residualRms, 2) + " %, median residual " +
                   formatNumber(qualityReport.medianResidual, 2) + " %\n";
        }

    }

    cv::Mat OutputClass::correctTile(const cv::Mat& tile, double gain, double fieldStrength) {
        if (tile.empty()) return {};
        const int channelCount = tile.channels();
        const bool isFloatingPoint = tile.depth() == CV_32F || tile.depth() == CV_64F;
        // Entier : sortie 16 bits, valeurs ramenées sur 0-65535 (8 bits : × 257). Flottant : même type.
        const int outputDepth = isFloatingPoint ? tile.depth() : CV_16U;
        const double scale = isFloatingPoint ? 1.0 : 65535.0 / PreparationClass::maximumValueOfDepth(tile.depth());
        const double maximumValue = isFloatingPoint ? 1.0 : 65535.0;
        const int colorChannelCount = channelCount == 4 ? 3 : channelCount;   // canal alpha recopié sans correction

        const int width = tile.cols, height = tile.rows;
        std::vector<double> squaredNormalizedX(width);
        for (int column = 0; column < width; ++column) {
            const double normalizedX = 2.0 * (column + 0.5) / width - 1.0;
            squaredNormalizedX[column] = normalizedX * normalizedX;
        }

        cv::Mat corrected(tile.size(), CV_MAKETYPE(outputDepth, channelCount));
        // Ligne par ligne, en parallèle : aucune copie flottante de la tuile entière
        cv::parallel_for_(cv::Range(0, height), [&](const cv::Range& rowRange) {
            cv::Mat inputRow, outputRow;
            std::vector<double> factors(width);
            for (int row = rowRange.start; row < rowRange.end; ++row) {
                const double normalizedY = 2.0 * (row + 0.5) / height - 1.0;
                for (int column = 0; column < width; ++column)
                    factors[column] = std::exp(-fieldStrength * (squaredNormalizedX[column] + normalizedY * normalizedY)) /
                                      gain * scale;

                tile.row(row).convertTo(inputRow, CV_MAKETYPE(CV_64F, channelCount));
                const double* inputValues = inputRow.ptr<double>();
                outputRow.create(1, width, CV_MAKETYPE(CV_64F, channelCount));
                double* outputValues = outputRow.ptr<double>();
                for (int column = 0; column < width; ++column)
                    for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                        const double value = inputValues[column * channelCount + channelIndex] *
                                             (channelIndex < colorChannelCount ? factors[column] : scale);
                        outputValues[column * channelCount + channelIndex] =
                            std::min(std::max(isFloatingPoint ? value : std::round(value), 0.0), maximumValue);
                    }
                outputRow.convertTo(corrected.row(row), corrected.type());
            }
        });
        return corrected;
    }

    std::string OutputClass::buildReport(const EqualizationResult& result) {
        std::string report = "Brightness equalization: " + std::to_string(result.sampleCount) + " samples\n";
        report += "  gains A (global gain)   : " + formatGains(result.gainsA) + "\n";
        report += "  gains B (gain + field)  : " + formatGains(result.gainsB) + "\n";
        report += "  field B: c = " + formatNumber(result.fieldStrength, 4) + " (border " +
                  formatNumber(std::exp(result.fieldStrength), 3) + ", corner " +
                  formatNumber(std::exp(2 * result.fieldStrength), 3) + ")\n";
        report += formatQualityLine("before:", result.before);
        report += formatQualityLine("A     :", result.afterA);
        report += formatQualityLine("B     :", result.afterB);
        if (!result.isolatedTiles.empty()) {
            report += "  tiles without a usable pair (gain 1, not equalized):";
            for (int tileIndex : result.isolatedTiles) report += " " + std::to_string(tileIndex);
            report += "\n";
        }
        if (!result.emptyPairs.empty()) {
            report += "  pairs without a sample (no usable overlap):";
            for (int pairIndex : result.emptyPairs) report += " " + std::to_string(pairIndex);
            report += "\n";
        }
        return report;
    }

    std::string OutputClass::describeNoSampleFailure(std::size_t pairCount) {
        const std::string pairsText = pairCount == 1 ? "the pair has no"
                                                     : "none of the " + std::to_string(pairCount) + " pairs has a";
        return "brightness equalization: " + pairsText + " usable pixel in its overlap "
               "(wrong translations, empty or saturated tiles, or margin too large)";
    }

}
