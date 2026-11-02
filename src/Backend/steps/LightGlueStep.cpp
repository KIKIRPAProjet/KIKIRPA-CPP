#include "steps/LightGlueStep.h"
#include "steps/MatchingStepUtils.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "core/Config.h"
#if defined(STITCHING_HAS_LIGHTGLUE)
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#endif
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stitching::steps {
namespace {
std::unique_ptr<matching::FeatureMatcher> createMatcher(const Json& params) {
#if defined(STITCHING_HAS_LIGHTGLUE)
    return std::make_unique<matching::LightGlueMatcher>(params);
#else
    (void)params;
    throw std::runtime_error("LightGlue unavailable: rebuild with ONNX Runtime and OpenCV dnn");
#endif
}

} // namespace

void to_json(Json& json, const LightGlueOutput& output) {
    json = matching_step::serializeOutput(output.key, output.value, output.type);
}

LightGlueStep::LightGlueStep(const LightGlueInput& input, MatcherFactory f)
    : factory(f ? std::move(f) : MatcherFactory(createMatcher)) {
    StepConfig cfg;
    cfg.name = "lightglue";
    cfg.options = {{"string", "modelPath", input.modelPath},
                   {"int", "minMatches", input.minMatches},
                   {"float", "overlap", input.overlap}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{output.type, output.key}};
    config = cfg;
}

bool LightGlueStep::execute() {
    output = {};
    outputData = {{"pairs", 0}, {"failures", Json::array()}};
    try {
        if (!data) throw std::runtime_error("DataStore is not attached");
        data->erase(output.key);
        // Les noms, types et doublons sont contrôlés par Step::importConfig().
        const auto model = option("modelPath").get<std::string>();
        const auto minimumValue = option("minMatches");
        const double overlap = option("overlap").get<double>();
        if (model.empty()) throw std::invalid_argument("modelPath must be non-empty");
        if (minimumValue < 1 || minimumValue > std::numeric_limits<int>::max())
            throw std::invalid_argument("minMatches must be a positive integer within int range");
        const int minimum = minimumValue.get<int>();
        if (!std::isfinite(overlap) || !(overlap > 0.0 && overlap < 1.0))
            throw std::invalid_argument("overlap must be in (0, 1)");
        const auto pairs = matching_step::collectPairs(data->get<Tiles>("tiles"));
        // Une erreur de chargement du modèle concerne toutes les paires.
        if (!matcher || loadedModel != model) {
            auto next = factory(Json{{"model_path", model}});
            if (!next) throw std::runtime_error("matcher factory returned null");
            matcher = std::move(next);
            loadedModel = model;
        }
        matching::EstimationRecouvrement estimator;
        cv::Mat a;
        const Tuile* loadedA = nullptr;
        for (const auto& pair : pairs) {
            try {
                if (loadedA != pair.a) {
                    a = matching_step::readImage(*pair.a);
                    loadedA = pair.a;
                }
                const cv::Mat b = matching_step::readImage(*pair.b);
                const PairContext context{pair.direction, static_cast<float>(overlap), std::nullopt};
                const auto estimated = estimator.estimer(a, b, context);
                if (!estimated) throw std::runtime_error("overlap estimation failed: " + estimator.raisonEchec());
                const auto regions = matching::EstimationRecouvrement::zonesCommunes(a.size(), b.size(), estimated->translation);
                if (regions.first.empty() || regions.second.empty())
                    throw std::runtime_error("tiles have no common region");
                PairMatches result{pair.a->getId(), pair.b->getId(), pair.direction, {}};
                if (!matcher->match(a(regions.first), b(regions.second), context, result.matches))
                    throw std::runtime_error("matcher could not find correspondences");
                matching_step::validateMatches(result.matches, regions.first.size(), regions.second.size(), minimum);
                matching_step::restoreCoordinates(result.matches, regions.first, regions.second);
                matching_step::validateMatches(result.matches, a.size(), b.size(), minimum);
                output.value.push_back(std::move(result));
            } catch (const std::exception& e) {
                outputData["failures"].push_back({{"idA", pair.a->getId()}, {"idB", pair.b->getId()}, {"reason", e.what()}});
            }
        }
        outputData["pairs"] = output.value.size();
        if (output.value.empty()) {
            outputData["error"] = "no pair could be measured";
            return false;
        }
        data->set(output.key, output.value);
        return true;
    } catch (const std::exception& e) {
        output = {};
        outputData["error"] = e.what();
        return false;
    }
}

} // namespace stitching::steps
