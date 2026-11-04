// src/backend/algorithms/brightness_equalization/brightness_equalizer.cpp
#include "algorithms/brightness_equalization/brightness_equalizer.h"

#include "algorithms/brightness_equalization/algorithm.h"
#include "algorithms/brightness_equalization/output.h"
#include "algorithms/brightness_equalization/preparation.h"

#include <cmath>
#include <cstddef>

namespace BrightnessEqualizationNs {

    BrightnessEqualizerClass::BrightnessEqualizerClass(const Parameters& initialParameters) : parameters(initialParameters) {
        PreparationClass::validateParameters(parameters);
    }

    ReducedTile BrightnessEqualizerClass::reduceTile(const cv::Mat& tile) const {
        return PreparationClass::reduceTile(tile, parameters);
    }

    ReducedTile BrightnessEqualizerClass::reduceTile(const cv::Mat& reducedImage, cv::Size fullSize) const {
        return PreparationClass::prepareReducedTile(reducedImage, fullSize, parameters);
    }

    std::optional<EqualizationResult> BrightnessEqualizerClass::estimate(const std::vector<ReducedTile>& tiles,
                                                                         const std::vector<NeighbourPair>& pairs) {
        failureReason = PreparationClass::validateInputs(tiles, pairs);
        if (!failureReason.empty()) return std::nullopt;

        // 1. Échantillons de chaque paire : écarts log dans les recouvrements
        EqualizationResult result;
        std::vector<PairSamples> pairSamples;
        for (std::size_t pairIndex = 0; pairIndex < pairs.size(); ++pairIndex) {
            auto samples = AlgorithmClass::collectSamples(tiles[pairs[pairIndex].tileIndexA], tiles[pairs[pairIndex].tileIndexB],
                                                          pairs[pairIndex], parameters);
            if (samples.logDifferences.empty()) {
                result.emptyPairs.push_back(static_cast<int>(pairIndex));
                continue;
            }
            result.sampleCount += samples.logDifferences.size();
            pairSamples.push_back(std::move(samples));
        }
        if (pairSamples.empty()) {
            failureReason = OutputClass::describeNoSampleFailure(pairs.size());
            return std::nullopt;
        }

        // 2. Moindres carrés robustes : A (gains seuls) puis B (gains + champ)
        const int tileCount = static_cast<int>(tiles.size());
        const Solution solutionA = AlgorithmClass::solveGains(pairSamples, tileCount, false, parameters);
        const Solution solutionB = AlgorithmClass::solveGains(pairSamples, tileCount, true, parameters);
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            result.gainsA.push_back(std::exp(solutionA.logGains[tileIndex]));
            result.gainsB.push_back(std::exp(solutionB.logGains[tileIndex]));
        }
        result.fieldStrength = solutionB.fieldStrength;
        std::vector<bool> isLinked(tileCount, false);
        for (const auto& samples : pairSamples) isLinked[samples.tileIndexA] = isLinked[samples.tileIndexB] = true;
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
            if (!isLinked[tileIndex]) result.isolatedTiles.push_back(tileIndex);

        // 3. Bilan avant / A / B
        AlgorithmClass::computeResiduals(pairSamples, {}, result.before);
        AlgorithmClass::computeResiduals(pairSamples, solutionA, result.afterA);
        AlgorithmClass::computeResiduals(pairSamples, solutionB, result.afterB);
        AlgorithmClass::measureOverlaps(tiles, pairs, {}, result.before);
        AlgorithmClass::measureOverlaps(tiles, pairs, solutionA, result.afterA);
        AlgorithmClass::measureOverlaps(tiles, pairs, solutionB, result.afterB);
        return result;
    }

    cv::Mat BrightnessEqualizerClass::correctTile(const cv::Mat& tile, const EqualizationResult& result, int tileIndex,
                                                  CorrectionMethod method) {
        return OutputClass::correctTile(tile, result.getGain(tileIndex, method), result.getFieldStrength(method));
    }

    std::vector<NeighbourPair> BrightnessEqualizerClass::buildPairsFromPositions(
        const std::vector<cv::Point2d>& positions, const std::vector<std::pair<int, int>>& neighbours) {
        std::vector<NeighbourPair> pairs;
        for (const auto& [tileIndexA, tileIndexB] : neighbours)
            pairs.push_back({tileIndexA, tileIndexB, positions.at(tileIndexB) - positions.at(tileIndexA)});
        return pairs;
    }

}
