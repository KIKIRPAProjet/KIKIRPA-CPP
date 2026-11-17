// src/backend/algorithms/brightness_equalization/output.cpp
#include "algorithms/brightness_equalization/output.h"

#include "algorithms/brightness_equalization/brightness_equalization_types.h"
#include "algorithms/brightness_equalization/preparation.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/utility.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace BrightnessEqualizationNs {

    namespace {

        constexpr int FORMATTED_NUMBER_SIZE = 64;      // tampon de formatNumber, largement suffisant pour un double
        constexpr double MAXIMUM_16_BIT_VALUE = 65535.0;

        // Profondeur de sortie et facteur d'échelle de la correction, selon la profondeur d'entrée.
        struct DepthConversion {
            int outputDepth = CV_16U;
            double scale = 1.0;                          // entier : ramène sur 0-65535 (8 bits : × 257)
            double maximumValue = MAXIMUM_16_BIT_VALUE;  // valeur maximale de la sortie
            bool isFloatingPoint = false;                // flottant : même type en sortie, pas d'arrondi
        };

        // Tout ce qu'il faut pour corriger une ligne de la région ; `corrected` partage ses pixels avec la sortie.
        struct RegionCorrection {
            cv::Mat region;
            cv::Mat corrected;
            RegionInTile placement;
            double gain = 1.0;
            double fieldStrength = 0.0;
            DepthConversion conversion;
            std::vector<double> squaredColumns;   // u² de chaque colonne de la région, dans le repère de la tuile
        };

        // Nombre écrit avec `decimalCount` chiffres après la virgule
        std::string formatNumber(double value, int decimalCount) {
            char buffer[FORMATTED_NUMBER_SIZE];
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

        DepthConversion makeDepthConversion(int depth) {
            if (depth == CV_32F || depth == CV_64F) return {depth, 1.0, 1.0, true};
            return {CV_16U, MAXIMUM_16_BIT_VALUE / PreparationClass::maximumValueOfDepth(depth), MAXIMUM_16_BIT_VALUE,
                    false};
        }

        // Carré de la coordonnée normalisée sur [−1, 1] du centre du pixel `pixelIndex`, sur `length` pixels.
        double squaredNormalizedCoordinate(int pixelIndex, int length) {
            const double normalizedCoordinate = 2.0 * (pixelIndex + 0.5) / length - 1.0;
            return normalizedCoordinate * normalizedCoordinate;
        }

        std::vector<double> computeSquaredColumns(const RegionInTile& placement, int regionWidth) {
            std::vector<double> squaredColumns(static_cast<std::size_t>(regionWidth));
            for (int column = 0; column < regionWidth; ++column)
                squaredColumns[static_cast<std::size_t>(column)] =
                    squaredNormalizedCoordinate(placement.origin.x + column, placement.tileSize.width);
            return squaredColumns;
        }

        // Facteur de chaque pixel de la ligne : exp(−c · r²) / gain, mis à l'échelle de la sortie.
        void computeRowFactors(const RegionCorrection& correction, int row, std::vector<double>& factors) {
            const double squaredRow = squaredNormalizedCoordinate(correction.placement.origin.y + row,
                                                                  correction.placement.tileSize.height);
            for (std::size_t column = 0; column < factors.size(); ++column)
                factors[column] = std::exp(-correction.fieldStrength * (correction.squaredColumns[column] + squaredRow)) /
                                  correction.gain * correction.conversion.scale;
        }

        // Ligne convertie en double, multipliée, arrondie et bornée, puis écrite dans le type de sortie : aucune copie
        // flottante de la région entière.
        void correctRow(const RegionCorrection& correction, int row, const std::vector<double>& factors) {
            const int channelCount = correction.region.channels();
            const int colorChannelCount = channelCount == 4 ? 3 : channelCount;   // canal alpha recopié sans correction
            const DepthConversion& conversion = correction.conversion;
            cv::Mat rowValues;
            correction.region.row(row).convertTo(rowValues, CV_MAKETYPE(CV_64F, channelCount));
            auto* values = rowValues.ptr<double>();
            for (int column = 0; column < rowValues.cols; ++column)
                for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                    double& value = values[column * channelCount + channelIndex];
                    value *= channelIndex < colorChannelCount ? factors[static_cast<std::size_t>(column)] : conversion.scale;
                    value = std::clamp(conversion.isFloatingPoint ? value : std::round(value), 0.0, conversion.maximumValue);
                }
            rowValues.convertTo(correction.corrected.row(row), correction.corrected.type());
        }

        void correctRows(const RegionCorrection& correction, const cv::Range& rowRange) {
            std::vector<double> factors(static_cast<std::size_t>(correction.region.cols));
            for (int row = rowRange.start; row < rowRange.end; ++row) {
                computeRowFactors(correction, row, factors);
                correctRow(correction, row, factors);
            }
        }

    }

    cv::Mat OutputClass::correctTile(const cv::Mat& tile, double gain, double fieldStrength) {
        return correctRegion(tile, {cv::Point(0, 0), tile.size()}, gain, fieldStrength);
    }

    cv::Mat OutputClass::correctRegion(const cv::Mat& region, const RegionInTile& placement, double gain,
                                       double fieldStrength) {
        if (region.empty()) return {};
        const DepthConversion conversion = makeDepthConversion(region.depth());
        const RegionCorrection correction{
            region, cv::Mat(region.size(), CV_MAKETYPE(conversion.outputDepth, region.channels())), placement, gain,
            fieldStrength, conversion, computeSquaredColumns(placement, region.cols)};
        // Ligne par ligne, en parallèle
        cv::parallel_for_(cv::Range(0, region.rows),
                          [&correction](const cv::Range& rowRange) { correctRows(correction, rowRange); });
        return correction.corrected;
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
            report += '\n';
        }
        if (!result.emptyPairs.empty()) {
            report += "  pairs without a sample (no usable overlap):";
            for (int pairIndex : result.emptyPairs) report += " " + std::to_string(pairIndex);
            report += '\n';
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
