// src/backend/steps/lightglue_step.cpp
#include "steps/lightglue_step.h"

#include "algorithms/stitching/feature_matcher.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "steps/matching_step_utils.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <exception>
#include "algorithms/stitching/lightglue/lightglue_matcher.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace {

        std::unique_ptr<StitchingNs::FeatureMatcherClass> createMatcher(const nlohmann::json& parameters) {
            return std::make_unique<LightGlueNs::LightGlueMatcherClass>(parameters);
        }

        struct PairProcessingContext {
            const LightGlueInput& input;
            StitchingNs::FeatureMatcherClass& matcher;
            OverlapEstimationNs::OverlapEstimatorClass& estimator;
            const DataNs::OverlapEstimates* overlapEstimates = nullptr;   // sortie de overlap-estimation, si présente
        };

        struct PairMeasurement {
            std::optional<DataNs::PairMatches> value;
            std::string error;
        };

        PairMeasurement measurePair(const NeighborPair& pair, const cv::Mat& imageA,
                                    const cv::Mat& imageB, const PairProcessingContext& context) {
            const float overlap = overlapForDirection(pair.direction, context.input.overlap, context.input.verticalOverlap);
            const DataNs::PairContext pairContext{pair.direction, overlap, std::nullopt};
            // Première passe : la mesure de overlap-estimation si elle existe pour cette paire, sinon la nôtre.
            auto translation = findEstimatedTranslation(context.overlapEstimates, pair.tileA->getId(), pair.tileB->getId());
            if (!translation) {
                const auto firstPass = context.estimator.estimate(imageA, imageB, pairContext);
                if (!firstPass) return {std::nullopt, "overlap estimation failed: " + context.estimator.getFailureReason()};
                translation = firstPass->translationBInA;
            }
            const auto regions = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(imageA.size(), imageB.size(), *translation);
            if (regions.first.empty() || regions.second.empty()) return {std::nullopt, "tiles have no common region"};
            DataNs::PairMatches pairOutput{pair.tileA->getId(), pair.tileB->getId(), pair.direction, {}};
            if (!context.matcher.match(imageA(regions.first), imageB(regions.second), pairContext, pairOutput.featureMatches))
                return {std::nullopt, "matcher could not find correspondences"};
            validateMatches(pairOutput.featureMatches, regions.first.size(), regions.second.size(), context.input.minMatches);
            restoreCoordinates(pairOutput.featureMatches, regions.first, regions.second);
            validateMatches(pairOutput.featureMatches, imageA.size(), imageB.size(), context.input.minMatches);
            return {std::move(pairOutput), {}};
        }

        constexpr int NO_CACHED_TILE = -1;   // collectPairs refuse les identifiants négatifs

        // Le cache retient l'identifiant de la tuile, pas son pointeur : comparer un pointeur au nullptr initial faisait
        // supposer à clang-analyzer une tuile nulle, que collectPairs exclut pourtant.
        struct ImageCache {
            cv::Mat pixels;
            int tileId = NO_CACHED_TILE;   // identifiant de la tuile dont pixels contient l'image
        };

        PairMeasurement tryMeasurePair(const NeighborPair& pair, const PairProcessingContext& context,
                                       ImageCache& cache) {
            try {
                if (cache.tileId != pair.tileA->getId()) {
                    cache.pixels = readImage(*pair.tileA);
                    cache.tileId = pair.tileA->getId();
                }
                const cv::Mat imageB = readImage(*pair.tileB);
                return measurePair(pair, cache.pixels, imageB, context);
            } catch (const std::exception& exception) {
                return {std::nullopt, exception.what()};
            }
        }

        void measurePairs(const std::vector<NeighborPair>& pairs, const PairProcessingContext& context,
                          LightGlueOutput& output, nlohmann::json& result) {
            ImageCache cache;
            for (const auto& pair : pairs) {
                auto measurement = tryMeasurePair(pair, context, cache);
                if (measurement.value) {
                    output.value.push_back(std::move(*measurement.value));
                } else {
                    result["failures"].push_back({{"idA", pair.tileA->getId()}, {"idB", pair.tileB->getId()},
                                                  {"reason", measurement.error}});
                }
            }
        }

    }

    void to_json(nlohmann::json& json, const LightGlueOutput& output) {
        json = serializeOutput(output.key, output.value, output.type);
    }

    LightGlueStepClass::LightGlueStepClass(const LightGlueInput& input, MatcherFactory matcherFactory)
        : factory(matcherFactory ? std::move(matcherFactory) : MatcherFactory(createMatcher)) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "lightglue";
        stepConfig.parameters = {{"string", "modelPath", input.modelPath}, {"int", "minMatches", input.minMatches},
                                 {"float", "overlap", input.overlap}, {"float", "verticalOverlap", input.verticalOverlap},
                                 {"float", "railMargin", input.railMargin}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    LightGlueInput LightGlueStepClass::readInput() const {
        LightGlueInput input;
        input.modelPath = getParameter("modelPath").get<std::string>();
        const auto minimumMatchCount = getParameter("minMatches");
        if (input.modelPath.empty()) throw std::invalid_argument("modelPath must be non-empty");
        if (minimumMatchCount < 1 || minimumMatchCount > std::numeric_limits<int>::max())
            throw std::invalid_argument("minMatches must be a positive integer within int range");
        input.minMatches = minimumMatchCount.get<int>();
        input.overlap = getParameter("overlap").get<float>();
        input.verticalOverlap = getParameter("verticalOverlap").get<float>();
        input.railMargin = getParameter("railMargin").get<double>();
        const std::string overlapError = checkOverlaps(input.overlap, input.verticalOverlap);
        if (!overlapError.empty()) throw std::invalid_argument(overlapError);
        if (!std::isfinite(input.railMargin) || input.railMargin < 0.0)
            throw std::invalid_argument("railMargin must be finite and nonnegative");
        return input;
    }

    void LightGlueStepClass::loadMatcher(const std::string& modelPath) {
        if (matcher && loadedModel == modelPath) return;
        auto nextMatcher = factory(nlohmann::json{{"model_path", modelPath}});
        if (!nextMatcher) throw std::runtime_error("matcher factory returned null");
        matcher = std::move(nextMatcher);
        loadedModel = modelPath;
    }

    bool LightGlueStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"failures", nlohmann::json::array()}};
        try {
            if (!dataStore) throw std::runtime_error("DataStore is not attached");
            dataStore->replace(output.key, std::vector<DataNs::PairMatches>{});
            const auto input = readInput();
            const auto pairs = collectPairs(dataStore->get<DataNs::TileList>("tiles"));
            loadMatcher(input.modelPath);
            PhaseCorrelationNs::Parameters firstPassParameters;
            firstPassParameters.railMargin = input.railMargin;
            OverlapEstimationNs::OverlapEstimatorClass estimator(firstPassParameters);
            const PairProcessingContext context{input, *matcher, estimator, findOverlapEstimates(*dataStore)};
            measurePairs(pairs, context, output, result);
            result["pairs"] = output.value.size();
            if (output.value.empty()) {
                result["error"] = "no pair could be measured";
                return false;
            }
            dataStore->replace(output.key, output.value);
            return true;
        } catch (const std::exception& exception) {
            output = {};
            result["error"] = exception.what();
            return false;
        }
    }

}
