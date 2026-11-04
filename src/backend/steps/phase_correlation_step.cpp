// src/backend/steps/phase_correlation_step.cpp
#include "steps/phase_correlation_step.h"

#include "core/config/pipeline_config.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace StepsNs {

    using Json = nlohmann::json;

    // Déclare l'identité du step : son nom, ses 12 paramètres (valeurs par défaut = celles de l'entrée), la donnée
    // qu'il lit (tuiles) et celle qu'il produit (appariements). Le pipeline s'en sert pour vérifier l'enchaînement.
    PhaseCorrelationStepClass::PhaseCorrelationStepClass(const PhaseCorrelationInput& initialInput) {
        const auto& correlationParameters = initialInput.parameters;
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "phase-correlation";
        stepConfig.parameters = {{"float", "overlap", initialInput.overlap},
                                 {"int", "niveau", initialInput.level},
                                 {"float", "lissage", correlationParameters.smoothingSigma},
                                 {"float", "seuilRapport", correlationParameters.minimumPeakRatio},
                                 {"float", "lissageLocal", correlationParameters.localSmoothingSigma},
                                 {"float", "ecartMax", correlationParameters.maximumLocalDeviation},
                                 {"float", "espacementMin", correlationParameters.minimumPointSpacing},
                                 {"float", "marge", correlationParameters.railMargin},
                                 {"float", "fractionValideMin", correlationParameters.minimumValidFraction},
                                 {"int", "nbPoints", correlationParameters.maximumPointCount},
                                 {"int", "fenetre", correlationParameters.patchSize},
                                 {"int", "facteurDft", correlationParameters.upsamplingFactor}};
        stepConfig.inputs = {{"Tiles", "tiles"}};
        stepConfig.outputs = {{"PairMatches", "matches"}};
        configJson = stepConfig;
    }

    cv::Mat PhaseCorrelationStepClass::readPixels(const DataNs::TileClass& tile, int level) {
        cv::Mat image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
        // Mêmes réductions successives que createPyramidalTiledTiff : le résultat sera celui du niveau du TIFF.
        for (int levelIndex = 0; levelIndex < level && !image.empty(); ++levelIndex)
            cv::resize(image, image, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        return image;
    }

    void PhaseCorrelationStepClass::scaleToFullResolution(DataNs::Matches& matches, int level) {
        if (level == 0) return;
        const float scale = static_cast<float>(1 << level);   // 2^level
        // Le pixel p du niveau réduit moyenne les pixels [p·scale, (p + 1)·scale[ : son centre est en (p + 0,5)·scale − 0,5.
        auto convertPoint = [scale](cv::Point2f& point) { point = (point + cv::Point2f(0.5f, 0.5f)) * scale - cv::Point2f(0.5f, 0.5f); };
        for (auto& point : matches.pointsInA) convertPoint(point);
        for (auto& point : matches.pointsInB) convertPoint(point);
        matches.translationX *= scale;
        matches.translationY *= scale;
    }

    // Étapes : 1. lire et contrôler les paramètres ; 2. créer l'algorithme ; 3. pour chaque tuile et chacune de ses
    // voisines (droite, dessous), lire les deux images, mesurer, ramener en pleine résolution ; 4. publier les résultats.
    bool PhaseCorrelationStepClass::execute() {
        output = {};
        result = Json::object();

        // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par
        // défaut. Un paramètre absent (JSON écrit avant son ajout) garde la valeur par défaut de PhaseCorrelationInput.
        PhaseCorrelationInput input;
        auto readParameter = [this](const char* parameterName, auto& value) {
            try {
                value = getParameter(parameterName).get<std::decay_t<decltype(value)>>();
            } catch (const std::out_of_range&) {}
        };
        try {
            auto& correlationParameters = input.parameters;
            readParameter("overlap", input.overlap);
            readParameter("niveau", input.level);
            readParameter("lissage", correlationParameters.smoothingSigma);
            readParameter("seuilRapport", correlationParameters.minimumPeakRatio);
            readParameter("lissageLocal", correlationParameters.localSmoothingSigma);
            readParameter("ecartMax", correlationParameters.maximumLocalDeviation);
            readParameter("espacementMin", correlationParameters.minimumPointSpacing);
            readParameter("marge", correlationParameters.railMargin);
            readParameter("fractionValideMin", correlationParameters.minimumValidFraction);
            readParameter("nbPoints", correlationParameters.maximumPointCount);
            readParameter("fenetre", correlationParameters.patchSize);
            readParameter("facteurDft", correlationParameters.upsamplingFactor);
        } catch (const Json::exception& exception) {
            result["error"] = std::string("invalid option type: ") + exception.what();
            return false;
        }

        // Contrôles : recouvrement dans ]0, 1[, niveau existant, tuiles présentes dans le DataStoreClass.
        if (!(input.overlap > 0.f && input.overlap < 1.f)) {
            result["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
            return false;
        }
        if (input.level < 0 || input.level >= DataNs::LEVEL_COUNT) {
            result["error"] = "niveau must be in [0, " + std::to_string(DataNs::LEVEL_COUNT - 1) + "], got "
                              + std::to_string(input.level);
            return false;
        }
        if (!dataStore || !dataStore->has("tiles")) {
            result["error"] = "no tiles: run the import step first";
            return false;
        }

        // L'algorithme valide ses réglages à la construction (std::invalid_argument si l'un est incohérent).
        std::optional<PhaseCorrelationNs::PhaseCorrelationMatcherClass> matcher;
        try {
            matcher.emplace(input.parameters);
        } catch (const std::invalid_argument& exception) {
            result["error"] = std::string("invalid parameters: ") + exception.what();
            return false;
        }

        // Au plus deux images en mémoire : la tuile courante (lue une seule fois pour ses deux voisines) et sa voisine.
        Json failures = Json::array();
        for (const auto& tile : dataStore->get<DataNs::TileList>("tiles")) {
            const std::pair<DataNs::TileClass*, DataNs::Direction> neighbours[] = {{tile->getRight(), DataNs::Direction::Right},
                                                                                    {tile->getBottom(), DataNs::Direction::Bottom}};
            cv::Mat tileImage;
            for (const auto& [neighbour, direction] : neighbours) {
                if (!neighbour) continue;
                if (tileImage.empty()) tileImage = readPixels(*tile, input.level);
                const cv::Mat neighbourImage = readPixels(*neighbour, input.level);
                if (tileImage.empty() || neighbourImage.empty()) {
                    result["error"] = "cannot read " + (tileImage.empty() ? tile.get() : neighbour)->getSource().string();
                    return false;
                }

                // Mesure sur les images du niveau analysé, puis conversion en pleine résolution. Une paire qui échoue
                // est notée avec la raison donnée par l'algorithme, sans arrêter le step.
                DataNs::PairMatches pair{tile->getId(), neighbour->getId(), direction, {}};
                if (matcher->match(tileImage, neighbourImage, DataNs::PairContext{direction, input.overlap}, pair.featureMatches)) {
                    scaleToFullResolution(pair.featureMatches, input.level);
                    output.pairs.push_back(std::move(pair));
                } else {
                    failures.push_back({{"idA", pair.tileIdA}, {"idB", pair.tileIdB}, {"reason", matcher->getFailureReason()}});
                }
            }
        }

        result["niveau"] = input.level;
        result["pairs"] = output.pairs.size();
        if (!failures.empty()) result["failures"] = failures;
        if (output.pairs.empty()) {
            result["error"] = "no pair could be measured";
            return false;
        }
        dataStore->replace("matches", output.pairs);   // pour les steps suivants (rejet des aberrants, placement global)
        return true;
    }

}
