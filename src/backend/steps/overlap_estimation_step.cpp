// src/backend/steps/overlap_estimation_step.cpp
#include "steps/overlap_estimation_step.h"

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "steps/matching_step_utils.h"
#include "steps/phase_correlation_step.h"

#include <opencv2/core/mat.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace {

        using Json = nlohmann::json;

        struct NeighbourPair {
            DataNs::TileClass* tileA;
            DataNs::TileClass* tileB;
            DataNs::Direction direction;
        };

        std::vector<NeighbourPair> findNeighbourPairs(const DataNs::TileList& tiles) {
            std::vector<NeighbourPair> pairs;
            std::set<std::pair<int, int>> visitedPairs;
            for (const auto& tile : tiles) {
                if (!tile) throw std::invalid_argument("overlap-estimation: tile list contains a null tile");
                for (const auto& neighbour : {std::pair{tile->getRight(), DataNs::Direction::Right},
                                               std::pair{tile->getBottom(), DataNs::Direction::Bottom}}) {
                    DataNs::TileClass* tileB = neighbour.first;
                    if (!tileB) continue;
                    const int firstTileId = tile->getId();
                    const int secondTileId = tileB->getId();
                    const std::pair<int, int> orderedPair{std::min(firstTileId, secondTileId),
                                                          std::max(firstTileId, secondTileId)};
                    if (visitedPairs.insert(orderedPair).second)
                        pairs.push_back({tile.get(), tileB, neighbour.second});
                }
            }
            return pairs;
        }

        constexpr double JSON_ROUNDING = 1000.0;   // recouvrement recalé arrondi au millième dans le résultat

        // Résultat d'une paire : l'estimation, ou la raison de l'échec ; et le recouvrement vu par la mesure.
        struct PairResult {
            std::optional<DataNs::PairOverlapEstimate> estimate;
            std::string failureReason;
            std::optional<OverlapObservation> observation;
        };

        // Une paire non mesurable ne fait pas échouer le step : sa raison est gardée, et la paire est simplement absente
        // du résultat.
        PairResult estimatePair(const NeighbourPair& pair, float overlap, OverlapEstimationNs::OverlapEstimatorClass& estimator) {
            const cv::Mat imageA = PhaseCorrelationStepClass::readPixels(*pair.tileA, 0);
            const cv::Mat imageB = PhaseCorrelationStepClass::readPixels(*pair.tileB, 0);
            if (imageA.empty() || imageB.empty())
                throw std::runtime_error("overlap-estimation: cannot read tile image");

            const DataNs::PairContext context{pair.direction, overlap, std::nullopt};
            const auto estimate = estimator.estimate(imageA, imageB, context);
            if (!estimate) {
                PairResult failure{std::nullopt, estimator.getFailureReason(), std::nullopt};
                if (const auto& peak = estimator.getOutOfMarginPeak())
                    failure.observation = OverlapObservation{pair.direction, peak->overlapFraction, true};
                return failure;
            }

            const auto commonAreas = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                imageA.size(), imageB.size(), estimate->translationBInA);
            if (commonAreas.first.empty() || commonAreas.second.empty())
                return {std::nullopt, "estimated tiles have no common area", std::nullopt};

            return {DataNs::PairOverlapEstimate{pair.tileA->getId(), pair.tileB->getId(), pair.direction, *estimate,
                                                commonAreas.first, commonAreas.second},
                    {}, OverlapObservation{pair.direction, estimate->overlapFraction, false}};
        }

        const char* directionName(DataNs::Direction direction) {
            return direction == DataNs::Direction::Bottom ? "Bottom" : "Right";
        }

        // Recalage du recouvrement nominal (voir calibrateOverlap) : pour chaque direction recalée, les paires refusées
        // sont estimées une seconde fois au recouvrement recalé. Le détail va dans result["overlapCalibration"].
        void retryWithCalibratedOverlaps(const std::vector<NeighbourPair>& pairs, const OverlapEstimationInput& input,
                                         OverlapEstimationNs::OverlapEstimatorClass& estimator,
                                         std::vector<PairResult>& results, Json& result) {
            std::vector<OverlapObservation> observations;
            for (const PairResult& pairResult : results)
                if (pairResult.observation) observations.push_back(*pairResult.observation);

            for (const DataNs::Direction direction : {DataNs::Direction::Right, DataNs::Direction::Bottom}) {
                const std::optional<double> calibrated = calibrateOverlap(observations, direction);
                if (!calibrated) continue;
                int recoveredCount = 0;
                for (std::size_t index = 0; index < pairs.size(); ++index) {
                    if (pairs[index].direction != direction || results[index].estimate) continue;
                    PairResult retry = estimatePair(pairs[index], static_cast<float>(*calibrated), estimator);
                    if (retry.estimate) {
                        results[index] = std::move(retry);
                        ++recoveredCount;
                    }
                }
                result["overlapCalibration"].push_back(
                    {{"dir", directionName(direction)},
                     {"nominal", std::round(overlapForDirection(direction, input.overlap, input.verticalOverlap) * JSON_ROUNDING) / JSON_ROUNDING},
                     {"calibrated", std::round(*calibrated * JSON_ROUNDING) / JSON_ROUNDING},
                     {"recoveredPairs", recoveredCount}});
            }
        }

    }

    OverlapEstimationStepClass::OverlapEstimationStepClass(const OverlapEstimationInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "overlap-estimation";
        stepConfig.parameters = {{"float", "overlap", input.overlap},
                                 {"float", "verticalOverlap", input.verticalOverlap},
                                 {"bool", "overlapCalibration", input.overlapCalibration},
                                 {"float", "smoothingSigma", input.parameters.smoothingSigma},
                                 {"float", "minimumPeakRatio", input.parameters.minimumPeakRatio},
                                 {"float", "railMargin", input.parameters.railMargin},
                                 {"float", "minimumValidFraction", input.parameters.minimumValidFraction}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    bool OverlapEstimationStepClass::execute() {
        output.value.clear();
        result = {{"pairs", 0}, {"failures", Json::array()}};
        try {
            if (!dataStore) throw std::invalid_argument("overlap-estimation: DataStore is not attached");
            dataStore->replace(output.key, DataNs::OverlapEstimates{});

            OverlapEstimationInput input;
            input.overlap = getParameter("overlap").get<float>();
            input.verticalOverlap = getParameter("verticalOverlap").get<float>();
            input.overlapCalibration = getParameter("overlapCalibration").get<bool>();
            const std::string overlapError = checkOverlaps(input.overlap, input.verticalOverlap);
            if (!overlapError.empty()) throw std::invalid_argument("overlap-estimation: " + overlapError);

            input.parameters.smoothingSigma = getParameter("smoothingSigma").get<double>();
            input.parameters.minimumPeakRatio = getParameter("minimumPeakRatio").get<double>();
            input.parameters.railMargin = getParameter("railMargin").get<double>();
            input.parameters.minimumValidFraction = getParameter("minimumValidFraction").get<double>();
            return estimatePairs(input);
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

    bool OverlapEstimationStepClass::estimatePairs(const OverlapEstimationInput& input) {
        // Première passe grossière (au pixel près) : une marge automatique plus large que celle de la corrélation de
        // phase, pour absorber un recouvrement réel éloigné du nominal ; les steps d'appariement affinent ensuite.
        PhaseCorrelationNs::Parameters parameters = input.parameters;
        parameters.automaticRailMarginFraction = OVERLAP_ESTIMATION_MARGIN_FRACTION;
        OverlapEstimationNs::OverlapEstimatorClass estimator(parameters);
        const auto pairs = findNeighbourPairs(dataStore->get<DataNs::TileList>("tiles"));
        if (pairs.empty()) throw std::runtime_error("overlap-estimation: no neighboring tile pairs");

        std::vector<PairResult> results;
        for (const NeighbourPair& pair : pairs)
            results.push_back(estimatePair(pair, overlapForDirection(pair.direction, input.overlap, input.verticalOverlap),
                                           estimator));
        if (input.overlapCalibration) retryWithCalibratedOverlaps(pairs, input, estimator, results, result);

        for (std::size_t index = 0; index < pairs.size(); ++index) {
            if (results[index].estimate) {
                output.value.push_back(*results[index].estimate);
            } else {
                result["failures"].push_back({{"idA", pairs[index].tileA->getId()}, {"idB", pairs[index].tileB->getId()},
                                              {"reason", results[index].failureReason}});
            }
        }
        result["pairs"] = output.value.size();
        if (output.value.empty()) throw std::runtime_error("overlap-estimation: no pair could be estimated");
        dataStore->replace(output.key, output.value);
        return true;
    }

}
