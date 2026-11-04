#include "backend/steps/RecouvrementStep.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#else
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#endif

#include <nlohmann/json.hpp>

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace StepsNs {

namespace {

using Json = nlohmann::json;

constexpr double PYRAMID_SCALE = 0.5;

#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
using Estimator = OverlapEstimationNs::OverlapEstimatorClass;
#else
using Estimator = stitching::matching::EstimationRecouvrement;
#endif

struct Neighbour {
    DataNs::TileClass* tile;
    DataNs::Direction direction;
};

struct ExecutionContext {
    const RecouvrementInput& input;
    Estimator& estimator;
    RecouvrementOutput& output;
    Json& result;
    Json failures = Json::array();
};

std::optional<DataNs::OverlapEstimate> estimate(Estimator& estimator, const cv::Mat& imageA, const cv::Mat& imageB,
                                                const DataNs::PairContext& context) {
#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
    return estimator.estimate(imageA, imageB, context);
#else
    return estimator.estimer(imageA, imageB, context);
#endif
}

const std::string& failureReason(Estimator& estimator) {
#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
    return estimator.getFailureReason();
#else
    return estimator.raisonEchec();
#endif
}

std::pair<cv::Rect, cv::Rect> commonAreas(cv::Size sizeA, cv::Size sizeB, cv::Point2d translation) {
#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
    return Estimator::computeCommonAreas(sizeA, sizeB, translation);
#else
    return Estimator::zonesCommunes(sizeA, sizeB, translation);
#endif
}

void recordFailure(ExecutionContext& execution, const DataNs::TileClass& tile,
                   const Neighbour& neighbour, const std::string& reason) {
    execution.failures.push_back({{"idA", tile.getId()},
                                  {"idB", neighbour.tile->getId()},
                                  {"direction", neighbour.direction == DataNs::Direction::Right ? "Right" : "Bottom"},
                                  {"reason", reason}});
}

void appendEstimate(ExecutionContext& execution, const DataNs::TileClass& tile, const Neighbour& neighbour,
                    const DataNs::OverlapEstimate& estimateResult) {
    double scale = static_cast<double>(1 << execution.input.niveau);
    DataNs::OverlapEstimate fullResolution = estimateResult;
    fullResolution.translationBInA *= scale;

    cv::Size sizeA(tile.getLevel(0).width, tile.getLevel(0).height);
    cv::Size sizeB(neighbour.tile->getLevel(0).width, neighbour.tile->getLevel(0).height);
    auto areas = commonAreas(sizeA, sizeB, fullResolution.translationBInA);
    if (areas.first.empty() || areas.second.empty()) {
        recordFailure(execution, tile, neighbour, "estimated translation produces no overlapping area");
        return;
    }

    execution.output.pairs.push_back(
        {tile.getId(), neighbour.tile->getId(), neighbour.direction, fullResolution, areas.first, areas.second});
}

bool processNeighbour(ExecutionContext& execution, const DataNs::TileClass& tile, const Neighbour& neighbour,
                      cv::Mat& imageA) {
    if (!neighbour.tile) return true;

    if (imageA.empty()) imageA = RecouvrementStepClass::readPixels(tile, execution.input.niveau);
    cv::Mat imageB = RecouvrementStepClass::readPixels(*neighbour.tile, execution.input.niveau);
    if (imageA.empty() || imageB.empty()) {
        auto* unreadable = imageA.empty() ? &tile : neighbour.tile;
        execution.result["error"] = "cannot read " + unreadable->getSource().string();
        return false;
    }

    DataNs::PairContext context{neighbour.direction, execution.input.overlap, std::nullopt};
    auto estimateResult = estimate(execution.estimator, imageA, imageB, context);
    if (!estimateResult) {
        recordFailure(execution, tile, neighbour, failureReason(execution.estimator));
        return true;
    }
    appendEstimate(execution, tile, neighbour, *estimateResult);
    return true;
}

bool processTiles(ExecutionContext& execution, const DataNs::TileList& tiles) {
    for (auto& tile : tiles) {
        std::array<Neighbour, 2> neighbours = {{{tile->getRight(), DataNs::Direction::Right},
                                                {tile->getBottom(), DataNs::Direction::Bottom}}};
        cv::Mat imageA;
        for (auto& neighbour : neighbours) {
            if (!processNeighbour(execution, *tile, neighbour, imageA)) return false;
        }
    }
    return true;
}

bool validateInput(const RecouvrementInput& input, Json& result) {
    if (!(input.overlap > 0.f && input.overlap < 1.f)) {
        result["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
        return false;
    }
    if (input.niveau < 0 || input.niveau >= DataNs::LEVEL_COUNT) {
        result["error"] = "niveau must be in [0, " + std::to_string(DataNs::LEVEL_COUNT - 1)
                          + "], got " + std::to_string(input.niveau);
        return false;
    }
    return true;
}

bool publishResults(ExecutionContext& execution, StepNs::DataStoreClass& dataStore) {
    execution.result["niveau"] = execution.input.niveau;
    execution.result["pairs"] = execution.output.pairs.size();
    if (!execution.failures.empty()) execution.result["failures"] = execution.failures;
    if (execution.output.pairs.empty()) {
        execution.result["error"] = "no neighboring pair could be estimated";
        return false;
    }
    dataStore.replace("overlap-estimates", execution.output.pairs);
    return true;
}

} // namespace

RecouvrementStepClass::RecouvrementStepClass(const RecouvrementInput& initialInput) {
    auto& p = initialInput.parametres;
    ConfigNs::StepConfig config;
    config.name = "recouvrement";
#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
    config.parameters = {{"float", "overlap", initialInput.overlap},
                         {"int", "niveau", initialInput.niveau},
                         {"float", "lissage", p.smoothingSigma},
                         {"float", "seuilRapport", p.minimumPeakRatio},
                         {"float", "marge", p.railMargin},
                         {"float", "fractionValideMin", p.minimumValidFraction}};
#else
    config.parameters = {{"float", "overlap", initialInput.overlap},
                         {"int", "niveau", initialInput.niveau},
                         {"float", "lissage", p.lissage},
                         {"float", "seuilRapport", p.seuilRapport},
                         {"float", "marge", p.marge},
                         {"float", "fractionValideMin", p.fractionValideMin}};
#endif
    config.inputs = {{"Tiles", "tiles"}};
    config.outputs = {{"OverlapEstimates", "overlap-estimates"}};
    configJson = config;
}

cv::Mat RecouvrementStepClass::readPixels(const DataNs::TileClass& tile, int level) {
    cv::Mat image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
    for (int levelIndex = 0; levelIndex < level && !image.empty(); ++levelIndex)
        cv::resize(image, image, cv::Size(), PYRAMID_SCALE, PYRAMID_SCALE, cv::INTER_AREA);
    return image;
}

bool RecouvrementStepClass::readInput(RecouvrementInput& input) {
    auto readParameter = [this](const char* name, auto& value) {
        try {
            value = getParameter(name).get<std::decay_t<decltype(value)>>();
        } catch (const std::out_of_range&) {
            // Older pipeline configurations may omit parameters added later.
            return;
        }
    };

    try {
        readParameter("overlap", input.overlap);
        readParameter("niveau", input.niveau);
#ifdef KIKIRPA_RECOUVREMENT_NEW_ESTIMATOR_API
        readParameter("lissage", input.parametres.smoothingSigma);
        readParameter("seuilRapport", input.parametres.minimumPeakRatio);
        readParameter("marge", input.parametres.railMargin);
        readParameter("fractionValideMin", input.parametres.minimumValidFraction);
#else
        readParameter("lissage", input.parametres.lissage);
        readParameter("seuilRapport", input.parametres.seuilRapport);
        readParameter("marge", input.parametres.marge);
        readParameter("fractionValideMin", input.parametres.fractionValideMin);
#endif
    } catch (const Json::exception& exception) {
        result["error"] = std::string("invalid option type: ") + exception.what();
        return false;
    }
    return true;
}

bool RecouvrementStepClass::execute() {
    output = {};
    result = Json::object();
    RecouvrementInput input;
    if (!readInput(input) || !validateInput(input, result)) return false;
    if (!dataStore || !dataStore->has("tiles")) {
        result["error"] = "no tiles: run the import step first";
        return false;
    }

    std::optional<Estimator> estimator;
    try {
        estimator.emplace(input.parametres);
    } catch (const std::exception& exception) {
        result["error"] = std::string("invalid parameters: ") + exception.what();
        return false;
    }

    ExecutionContext execution{input, *estimator, output, result};
    const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
    if (!processTiles(execution, tiles)) return false;
    return publishResults(execution, *dataStore);
}

} // namespace StepsNs
