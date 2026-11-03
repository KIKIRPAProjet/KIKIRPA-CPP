// src/backend/steps/sift_step.cpp
#include "steps/sift_step.h"

#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "algorithms/stitching/sift/SiftMatcher.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "steps/matching_step_utils.h"

#include <exception>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cmath>
#include <limits>
#include <opencv2/core/mat.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace {

        constexpr int MINIMUM_SIFT_MATCH_COUNT = 4;

        struct PairProcessingContext {
            const SiftInput& input;
            stitching::matching::SiftMatcher& matcher;
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
            if (regions.first.empty() || regions.second.empty())
                return {std::nullopt, "tiles have no common region"};
            DataNs::PairMatches pairOutput{pair.tileA->getId(), pair.tileB->getId(), pair.direction, {}};
            if (!context.matcher.match(imageA(regions.first), imageB(regions.second), pairContext, pairOutput.featureMatches))
                return {std::nullopt, "SIFT found fewer than four correspondences"};
            validateMatches(pairOutput.featureMatches, regions.first.size(), regions.second.size(), context.input.minMatches);
            restoreCoordinates(pairOutput.featureMatches, regions.first, regions.second);
            validateMatches(pairOutput.featureMatches, imageA.size(), imageB.size(), context.input.minMatches);
            return {std::move(pairOutput), {}};
        }

        struct ImageCache {
            cv::Mat pixels;
            const DataNs::TileClass* tile = nullptr;
        };

        PairMeasurement tryMeasurePair(const NeighborPair& pair, const PairProcessingContext& context,
                                       ImageCache& cache) {
            try {
                if (cache.tile != pair.tileA) {
                    cache.pixels = readImage(*pair.tileA);
                    cache.tile = pair.tileA;
                }
                const cv::Mat imageB = readImage(*pair.tileB);
                return measurePair(pair, cache.pixels, imageB, context);
            } catch (const std::exception& exception) {
                return {std::nullopt, exception.what()};
            }
        }

        void measurePairs(const std::vector<NeighborPair>& pairs, const PairProcessingContext& context,
                          SiftOutput& output, nlohmann::json& result) {
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

    void to_json(nlohmann::json& json, const SiftOutput& output) {
        json = serializeOutput(output.key, output.value, output.type);
    }

    SiftStepClass::SiftStepClass(const SiftInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "sift";
        stepConfig.parameters = {{"float", "overlap", input.overlap}, {"int", "maxFeatures", input.maxFeatures},
                                 {"float", "loweRatio", input.loweRatio}, {"float", "contrastThreshold", input.contrastThreshold},
                                 {"float", "claheClipLimit", input.claheClipLimit}, {"int", "minMatches", input.minMatches}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    SiftInput SiftStepClass::readInput() const {
        SiftInput input;
        // La structure, les noms et les types sont contrôlés par StepClass::importConfig().
        const auto readCount = [this](const char* name) {
            const auto value = getParameter(name);
            if (value < MINIMUM_SIFT_MATCH_COUNT || value > std::numeric_limits<int>::max())
                throw std::invalid_argument(std::string(name) + " must be in [4, INT_MAX]");
            return value.get<int>();
        };
        input.maxFeatures = readCount("maxFeatures");
        input.minMatches = readCount("minMatches");
        input.overlap = getParameter("overlap").get<float>();
        input.loweRatio = getParameter("loweRatio").get<float>();
        input.contrastThreshold = getParameter("contrastThreshold").get<double>();
        input.claheClipLimit = getParameter("claheClipLimit").get<double>();
        if (!std::isfinite(input.overlap) || !(input.overlap > 0.f && input.overlap < 1.f))
            throw std::invalid_argument("overlap must be in (0, 1)");
        if (!std::isfinite(input.loweRatio) || !(input.loweRatio > 0.f && input.loweRatio < 1.f))
            throw std::invalid_argument("loweRatio must be in (0, 1)");
        if (!std::isfinite(input.contrastThreshold) || input.contrastThreshold <= 0.0)
            throw std::invalid_argument("contrastThreshold must be finite and positive");
        if (!std::isfinite(input.claheClipLimit) || input.claheClipLimit < 0.0)
            throw std::invalid_argument("claheClipLimit must be finite and nonnegative");
        if (input.minMatches > input.maxFeatures)
            throw std::invalid_argument("minMatches cannot exceed maxFeatures");
        return input;
    }

    bool SiftStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"failures", nlohmann::json::array()}};
        try {
            if (!dataStore) throw std::runtime_error("DataStore is not attached");
            dataStore->replace(output.key, std::vector<DataNs::PairMatches>{});
            const auto input = readInput();
            const auto pairs = collectPairs(dataStore->get<DataNs::TileList>("tiles"));
            // L'API de l'algorithme conserve ses paramètres internes en snake_case.
            stitching::matching::SiftMatcher matcher(nlohmann::json{{"max_features", input.maxFeatures}, {"lowe_ratio", input.loweRatio},
                                                         {"contrast_threshold", input.contrastThreshold},
                                                         {"clahe_clip_limit", input.claheClipLimit}});
            stitching::matching::EstimationRecouvrement estimator;
            const PairProcessingContext context{input, matcher, estimator};
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
