// src/backend/steps/sift_step.cpp
#include "steps/sift_step.h"
#include "steps/matching_step_utils.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "algorithms/stitching/sift/SiftMatcher.h"
#include "core/config/pipeline_config.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace StepsNs {

    void to_json(nlohmann::json& json, const SiftOutput& output) {
        json = MatchingNs::serializeOutput(output.key, output.value, output.type);
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
            if (value < 4 || value > std::numeric_limits<int>::max())
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
            const auto pairs = MatchingNs::collectPairs(dataStore->get<DataNs::TileList>("tiles"));
            // L'API de l'algorithme conserve ses paramètres internes en snake_case.
            stitching::matching::SiftMatcher matcher(nlohmann::json{{"max_features", input.maxFeatures}, {"lowe_ratio", input.loweRatio},
                                                         {"contrast_threshold", input.contrastThreshold},
                                                         {"clahe_clip_limit", input.claheClipLimit}});
            stitching::matching::EstimationRecouvrement estimator;
            cv::Mat imageA;
            const DataNs::TileClass* loadedTile = nullptr;
            for (const auto& pair : pairs) {
                try {
                    if (loadedTile != pair.a) {
                        imageA = MatchingNs::readImage(*pair.a);
                        loadedTile = pair.a;
                    }
                    const cv::Mat imageB = MatchingNs::readImage(*pair.b);
                    const DataNs::PairContext context{pair.direction, input.overlap, std::nullopt};
                    const auto overlap = estimator.estimer(imageA, imageB, context);
                    if (!overlap) throw std::runtime_error("overlap estimation failed: " + estimator.raisonEchec());
                    const auto regions = stitching::matching::EstimationRecouvrement::zonesCommunes(
                        imageA.size(), imageB.size(), overlap->translationBInA);
                    if (regions.first.empty() || regions.second.empty())
                        throw std::runtime_error("tiles have no common region");
                    DataNs::PairMatches pairOutput{pair.a->getId(), pair.b->getId(), pair.direction, {}};
                    if (!matcher.match(imageA(regions.first), imageB(regions.second), context, pairOutput.featureMatches))
                        throw std::runtime_error("SIFT found fewer than four correspondences");
                    MatchingNs::validateMatches(pairOutput.featureMatches, regions.first.size(), regions.second.size(), input.minMatches);
                    MatchingNs::restoreCoordinates(pairOutput.featureMatches, regions.first, regions.second);
                    MatchingNs::validateMatches(pairOutput.featureMatches, imageA.size(), imageB.size(), input.minMatches);
                    output.value.push_back(std::move(pairOutput));
                } catch (const std::exception& exception) {
                    result["failures"].push_back({{"idA", pair.a->getId()}, {"idB", pair.b->getId()}, {"reason", exception.what()}});
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
