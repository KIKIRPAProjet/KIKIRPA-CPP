#include "steps/RecouvrementStep.h"

#include "core/Config.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace stitching::steps {

RecouvrementStep::RecouvrementStep(const RecouvrementInput& input) {
    const auto& p = input.parametres;
    StepConfig cfg;
    cfg.name = "recouvrement";
    cfg.options = {
        {"float", "overlap",           input.overlap},
        {"float", "lissage",           static_cast<float>(p.lissage)},
        {"float", "seuilRapport",      static_cast<float>(p.seuilRapport)},
        {"float", "marge",             static_cast<float>(p.marge)},
        {"float", "fractionValideMin", static_cast<float>(p.fractionValideMin)}
    };
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{"Recouvrements", "recouvrements"}};
    config = cfg;
}

RecouvrementStep::RecouvrementStep(
    float overlap,
    const matching::correlation_phase::Parametres& parametres)
    : RecouvrementStep(RecouvrementInput{overlap, parametres}) {}

bool RecouvrementStep::execute() {
    output = {};
    outputData = Json::object();

    if (!data || !data->has("tiles")) {
        outputData["error"] = "no tiles: run the import step first";
        return false;
    }

    RecouvrementInput input;
    auto readFloat = [this](const std::string& key, float defaultVal) -> float {
        try {
            if (config.contains("options") && config["options"].is_array()) {
                for (const auto& o : config["options"]) {
                    if (o.value("key", "") == key && o.contains("value")) {
                        return o["value"].get<float>();
                    }
                }
            }
        } catch (...) {}
        return defaultVal;
    };

    input.overlap = readFloat("overlap", 0.2f);
    input.parametres.lissage = readFloat("lissage", static_cast<float>(input.parametres.lissage));
    input.parametres.seuilRapport = readFloat("seuilRapport", static_cast<float>(input.parametres.seuilRapport));
    input.parametres.marge = readFloat("marge", static_cast<float>(input.parametres.marge));
    input.parametres.fractionValideMin = readFloat("fractionValideMin", static_cast<float>(input.parametres.fractionValideMin));

    if (!(input.overlap > 0.f && input.overlap < 1.f)) {
        outputData["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
        return false;
    }

    std::optional<matching::EstimationRecouvrement> estimator;
    try {
        estimator.emplace(input.parametres);
    } catch (const std::invalid_argument& e) {
        outputData["error"] = std::string("invalid estimator parameters: ") + e.what();
        return false;
    }

    const auto& tiles = data->get<Tiles>("tiles");
    Json failures = Json::array();

    // Lecture à la demande : au plus deux images en mémoire à la fois.
    // Respecte les données scientifiques 16-bit et multicanal via IMREAD_UNCHANGED.
    auto lire = [](const Tuile& tile) -> cv::Mat {
        if (!tile.getSource().empty()) {
            std::error_code ec;
            if (std::filesystem::exists(tile.getSource(), ec)) {
                cv::Mat img = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
                if (!img.empty()) return img;
            }
        }
        if (!tile.getTiff().empty()) {
            std::error_code ec;
            if (std::filesystem::exists(tile.getTiff(), ec)) {
                cv::Mat img = cv::imread(tile.getTiff().string(), cv::IMREAD_UNCHANGED);
                if (!img.empty()) return img;
            }
        }
        return {};
    };

    for (const auto& tilePtr : tiles) {
        if (!tilePtr) continue;
        const Tuile& tile = *tilePtr;
        const std::pair<Tuile*, Direction> neighbors[] = {
            {tile.getRight(), Direction::Right},
            {tile.getBottom(), Direction::Bottom}
        };

        cv::Mat imageA;
        for (const auto& [neighbor, dir] : neighbors) {
            if (!neighbor) continue;

            if (imageA.empty()) imageA = lire(tile);
            if (imageA.empty()) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "cannot read source image for tile " + std::to_string(tile.getId())}
                });
                break;
            }

            const cv::Mat imageB = lire(*neighbor);
            if (imageB.empty()) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "cannot read source image for tile " + std::to_string(neighbor->getId())}
                });
                continue;
            }

            if (imageA.type() != imageB.type()) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "incompatible image types: " + std::to_string(imageA.type()) + " vs " + std::to_string(imageB.type())}
                });
                continue;
            }

            if (imageA.dims != 2 || imageB.dims != 2 || imageA.rows <= 0 || imageA.cols <= 0 || imageB.rows <= 0 || imageB.cols <= 0) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "invalid 2D image dimensions"}
                });
                continue;
            }

            if (imageA.size() != imageB.size()) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "incompatible image sizes: " + std::to_string(imageA.cols) + "x" + std::to_string(imageA.rows) +
                               " vs " + std::to_string(imageB.cols) + "x" + std::to_string(imageB.rows)}
                });
                continue;
            }

            const PairContext context{dir, input.overlap};
            const auto estimate = estimator->estimer(imageA, imageB, context);
            if (!estimate) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", estimator->raisonEchec().empty() ? "estimation failed" : estimator->raisonEchec()}
                });
                continue;
            }

            const auto zones = matching::EstimationRecouvrement::zonesCommunes(
                imageA.size(), imageB.size(), estimate->translation);
            if (zones.first.empty() || zones.second.empty()) {
                failures.push_back({
                    {"idA", tile.getId()}, {"idB", neighbor->getId()},
                    {"reason", "estimated tiles have no usable overlap"}
                });
                continue;
            }

            output.pairs.push_back(PairRecouvrement{
                tile.getId(), neighbor->getId(), dir, *estimate, zones.first, zones.second
            });
        }
    }

    outputData["pairs"] = output.pairs.size();
    if (!failures.empty()) outputData["failures"] = failures;

    if (output.pairs.empty()) {
        outputData["error"] = "no neighboring pair has a usable estimated overlap";
        return false;
    }

    if (data) {
        data->set("recouvrements", output.pairs);
    }
    return true;
}

} // namespace stitching::steps

