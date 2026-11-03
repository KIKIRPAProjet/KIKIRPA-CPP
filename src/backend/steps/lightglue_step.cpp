// src/backend/steps/lightglue_step.cpp
#include "steps/lightglue_step.h"

#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "core/config/pipeline_config.h"
#include "steps/matching_step_utils.h"
#if defined(STITCHING_HAS_LIGHTGLUE)
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#endif

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace {

        std::unique_ptr<stitching::matching::FeatureMatcher> createMatcher(const nlohmann::json& parameters) {
#if defined(STITCHING_HAS_LIGHTGLUE)
            return std::make_unique<stitching::matching::LightGlueMatcher>(parameters);
#else
            (void)parameters;
            throw std::runtime_error("LightGlue unavailable: rebuild with ONNX Runtime and OpenCV dnn");
#endif
        }

        struct PairProcessingContext {
            const LightGlueInput& input;
            stitching::matching::FeatureMatcher& matcher;
            stitching::matching::EstimationRecouvrement& estimator;
        };

        struct PairMeasurement {
            std::optional<DataNs::PairMatches> value;
            std::string error;
        };

        PairMeasurement measurePair(const NeighborPair& pair, const cv::Mat& imageA,
                                    const cv::Mat& imageB, const PairProcessingContext& context) {
            const DataNs::PairContext pairContext{pair.direction, context.input.overlap, std::nullopt};
            const auto overlap = context.estimator.estimer(imageA, imageB, pairContext);
            if (!overlap) return {std::nullopt, "overlap estimation failed: " + context.estimator.raisonEchec()};
            const auto regions = stitching::matching::EstimationRecouvrement::zonesCommunes(
                imageA.size(), imageB.size(), overlap->translationBInA);
            if (regions.first.empty() || regions.second.empty()) return {std::nullopt, "tiles have no common region"};
            DataNs::PairMatches pairOutput{pair.tileA->getId(), pair.tileB->getId(), pair.direction, {}};
            if (!context.matcher.match(imageA(regions.first), imageB(regions.second), pairContext, pairOutput.featureMatches))
                return {std::nullopt, "matcher could not find correspondences"};
            validateMatches(pairOutput.featureMatches, regions.first.size(), regions.second.size(), context.input.minMatches);
            restoreCoordinates(pairOutput.featureMatches, regions.first, regions.second);
            validateMatches(pairOutput.featureMatches, imageA.size(), imageB.size(), context.input.minMatches);
            return {std::move(pairOutput), {}};
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
                                 {"float", "overlap", input.overlap}};
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
        if (!std::isfinite(input.overlap) || !(input.overlap > 0.f && input.overlap < 1.f))
            throw std::invalid_argument("overlap must be in (0, 1)");
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
            stitching::matching::EstimationRecouvrement estimator;
            const PairProcessingContext context{input, *matcher, estimator};
            cv::Mat imageA;
            const DataNs::TileClass* loadedTile = nullptr;
            for (const auto& pair : pairs) {
                try {
                    if (loadedTile != pair.tileA) {
                        imageA = readImage(*pair.tileA);
                        loadedTile = pair.tileA;
                    }
                    const cv::Mat imageB = readImage(*pair.tileB);
                    auto measurement = measurePair(pair, imageA, imageB, context);
                    if (measurement.value) {
                        output.value.push_back(std::move(*measurement.value));
                    } else {
                        result["failures"].push_back({{"idA", pair.tileA->getId()}, {"idB", pair.tileB->getId()},
                                                      {"reason", measurement.error}});
                    }
                } catch (const std::exception& exception) {
                    result["failures"].push_back({{"idA", pair.tileA->getId()}, {"idB", pair.tileB->getId()},
                                                  {"reason", exception.what()}});
                }
            }
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
