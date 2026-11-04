#include "backend/steps/RecouvrementStep.h"

#include "core/config/pipeline_config.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace StepsNs {

using Json = nlohmann::json;
using OverlapEstimationNs::OverlapEstimatorClass;

RecouvrementStepClass::RecouvrementStepClass(const RecouvrementInput& initialInput) {
    const auto& p = initialInput.parametres;
    ConfigNs::StepConfig config;
    config.name = "recouvrement";
    config.parameters = {{"float", "overlap", initialInput.overlap},
                         {"int", "niveau", initialInput.niveau},
                         {"float", "lissage", p.smoothingSigma},
                         {"float", "seuilRapport", p.minimumPeakRatio},
                         {"float", "marge", p.railMargin},
                         {"float", "fractionValideMin", p.minimumValidFraction}};
    config.inputs = {{"Tiles", "tiles"}};
    config.outputs = {{"OverlapEstimates", "overlap-estimates"}};
    configJson = config;
}

cv::Mat RecouvrementStepClass::readPixels(const DataNs::TileClass& tile, int level) {
    cv::Mat image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
    for (int i = 0; i < level && !image.empty(); ++i)
        cv::resize(image, image, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
    return image;
}

bool RecouvrementStepClass::execute() {
    output = {};
    result = Json::object();

    RecouvrementInput input;
    auto readParameter = [this](const char* name, auto& value) {
        try {
            value = getParameter(name).get<std::decay_t<decltype(value)>>();
        } catch (const std::out_of_range&) {
            // Paramètre absent d'une ancienne configuration : garder la valeur par défaut.
        }
    };

    try {
        readParameter("overlap", input.overlap);
        readParameter("niveau", input.niveau);
        readParameter("lissage", input.parametres.smoothingSigma);
        readParameter("seuilRapport", input.parametres.minimumPeakRatio);
        readParameter("marge", input.parametres.railMargin);
        readParameter("fractionValideMin", input.parametres.minimumValidFraction);
    } catch (const Json::exception& e) {
        result["error"] = std::string("invalid option type: ") + e.what();
        return false;
    }

    if (!(input.overlap > 0.f && input.overlap < 1.f)) {
        result["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
        return false;
    }
    if (input.niveau < 0 || input.niveau >= DataNs::LEVEL_COUNT) {
        result["error"] = "niveau must be in [0, " + std::to_string(DataNs::LEVEL_COUNT - 1)
                          + "], got " + std::to_string(input.niveau);
        return false;
    }
    if (!dataStore || !dataStore->has("tiles")) {
        result["error"] = "no tiles: run the import step first";
        return false;
    }

    std::optional<OverlapEstimatorClass> estimator;
    try {
        estimator.emplace(input.parametres);
    } catch (const std::exception& e) {
        result["error"] = std::string("invalid parameters: ") + e.what();
        return false;
    }

    Json failures = Json::array();
    const auto& tiles = dataStore->get<DataNs::TileList>("tiles");

    for (const auto& tile : tiles) {
        const std::pair<DataNs::TileClass*, DataNs::Direction> neighbours[] = {
            {tile->getRight(), DataNs::Direction::Right},
            {tile->getBottom(), DataNs::Direction::Bottom}
        };

        cv::Mat imageA;
        for (const auto& [neighbour, direction] : neighbours) {
            if (!neighbour) continue;

            if (imageA.empty()) imageA = readPixels(*tile, input.niveau);
            const cv::Mat imageB = readPixels(*neighbour, input.niveau);
            if (imageA.empty() || imageB.empty()) {
                const auto* unreadable = imageA.empty() ? tile.get() : neighbour;
                result["error"] = "cannot read " + unreadable->getSource().string();
                return false;
            }

            const DataNs::PairContext context{direction, input.overlap, std::nullopt};
            const auto estimate = estimator->estimate(imageA, imageB, context);
            if (!estimate) {
                failures.push_back({{"idA", tile->getId()},
                                    {"idB", neighbour->getId()},
                                    {"direction", direction == DataNs::Direction::Right ? "Right" : "Bottom"},
                                    {"reason", estimator->getFailureReason()}});
                continue;
            }

            // Convertir la translation du niveau analysé vers les pixels de pleine résolution.
            const double scale = static_cast<double>(1 << input.niveau);
            DataNs::OverlapEstimate fullResolution = *estimate;
            fullResolution.translationBInA *= scale;

            const cv::Size sizeA(tile->getLevel(0).width, tile->getLevel(0).height);
            const cv::Size sizeB(neighbour->getLevel(0).width, neighbour->getLevel(0).height);
            const auto [zoneA, zoneB] = OverlapEstimatorClass::computeCommonAreas(
                sizeA, sizeB, fullResolution.translationBInA);

            if (zoneA.empty() || zoneB.empty()) {
                failures.push_back({{"idA", tile->getId()},
                                    {"idB", neighbour->getId()},
                                    {"direction", direction == DataNs::Direction::Right ? "Right" : "Bottom"},
                                    {"reason", "estimated translation produces no overlapping area"}});
                continue;
            }

            output.pairs.push_back({tile->getId(), neighbour->getId(), direction, fullResolution, zoneA, zoneB});
        }
    }

    result["niveau"] = input.niveau;
    result["pairs"] = output.pairs.size();
    if (!failures.empty()) result["failures"] = failures;
    if (output.pairs.empty()) {
        result["error"] = "no neighboring pair could be estimated";
        return false;
    }

    dataStore->replace("overlap-estimates", output.pairs);
    return true;
}

} // namespace StepsNs
