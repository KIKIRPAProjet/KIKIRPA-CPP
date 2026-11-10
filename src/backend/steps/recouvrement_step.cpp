// src/backend/steps/recouvrement_step.cpp
#include "steps/recouvrement_step.h"

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include "steps/phase_correlation_step.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
                if (!tile) throw std::invalid_argument("recouvrement: tile list contains a null tile");
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

        DataNs::PairOverlapEstimate estimatePair(const NeighbourPair& pair, float overlap,
                                                  OverlapEstimationNs::OverlapEstimatorClass& estimator,
                                                  Json& failures) {
            const cv::Mat imageA = PhaseCorrelationStepClass::readPixels(*pair.tileA, 0);
            const cv::Mat imageB = PhaseCorrelationStepClass::readPixels(*pair.tileB, 0);
            if (imageA.empty() || imageB.empty())
                throw std::runtime_error("recouvrement: cannot read tile image");

            const DataNs::PairContext context{pair.direction, overlap, std::nullopt};
            const auto estimate = estimator.estimate(imageA, imageB, context);
            if (!estimate) {
                failures.push_back({{"idA", pair.tileA->getId()}, {"idB", pair.tileB->getId()},
                                    {"reason", estimator.getFailureReason()}});
                return {};
            }

            const auto commonAreas = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                imageA.size(), imageB.size(), estimate->translationBInA);
            if (commonAreas.first.empty() || commonAreas.second.empty()) {
                failures.push_back({{"idA", pair.tileA->getId()}, {"idB", pair.tileB->getId()},
                                    {"reason", "estimated tiles have no common area"}});
                return {};
            }

            return {pair.tileA->getId(), pair.tileB->getId(), pair.direction, *estimate,
                    commonAreas.first, commonAreas.second};
        }

    }

    RecouvrementStepClass::RecouvrementStepClass(const RecouvrementInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "recouvrement";
        stepConfig.parameters = {{"float", "overlap", input.overlap},
                                 {"float", "smoothingSigma", static_cast<float>(input.parameters.smoothingSigma)},
                                 {"float", "minimumPeakRatio", static_cast<float>(input.parameters.minimumPeakRatio)},
                                 {"float", "railMargin", static_cast<float>(input.parameters.railMargin)},
                                 {"float", "minimumValidFraction", static_cast<float>(input.parameters.minimumValidFraction)}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    bool RecouvrementStepClass::execute() {
        output.value.clear();
        result = {{"pairs", 0}, {"failures", Json::array()}, {"error", ""}};
        try {
            if (!dataStore) throw std::invalid_argument("recouvrement: DataStore is not attached");
            dataStore->replace(output.key, DataNs::OverlapEstimates{});

            const float overlap = getParameter("overlap").get<float>();
            if (overlap <= 0.f || overlap >= 1.f)
                throw std::invalid_argument("recouvrement: overlap must be greater than 0 and less than 1");

            PhaseCorrelationNs::Parameters parameters;
            parameters.smoothingSigma = getParameter("smoothingSigma").get<float>();
            parameters.minimumPeakRatio = getParameter("minimumPeakRatio").get<float>();
            parameters.railMargin = getParameter("railMargin").get<float>();
            parameters.minimumValidFraction = getParameter("minimumValidFraction").get<float>();
            OverlapEstimationNs::OverlapEstimatorClass estimator(parameters);

            const auto pairs = findNeighbourPairs(dataStore->get<DataNs::TileList>("tiles"));
            if (pairs.empty()) throw std::runtime_error("recouvrement: no neighboring tile pairs");

            for (const NeighbourPair& pair : pairs) {
                const auto estimate = estimatePair(pair, overlap, estimator, result["failures"]);
                if (!estimate.commonAreaInA.empty()) output.value.push_back(estimate);
            }
            result["pairs"] = output.value.size();
            if (output.value.empty()) throw std::runtime_error("recouvrement: no pair could be estimated");

            dataStore->replace(output.key, output.value);
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

}
