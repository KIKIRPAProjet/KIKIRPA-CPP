// src/backend/steps/intensity_correction_step.cpp
#include "steps/intensity_correction_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/phase_correlation_step.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;
    using BrightnessEqualizationNs::BrightnessEqualizerClass;
    using BrightnessEqualizationNs::CorrectionMethod;
    using BrightnessEqualizationNs::EqualizationResult;
    using BrightnessEqualizationNs::NeighbourPair;
    using BrightnessEqualizationNs::ReducedTile;

    namespace {

        // Niveau de pyramide lu pour l'estimation : 1/8 de la résolution. Le modèle n'a que (tuiles + 1) inconnues,
        // une image réduite suffit, et une seule image pleine résolution est en mémoire à la fois.
        constexpr int ANALYSIS_LEVEL = 3;

        // Valeurs possibles du paramètre "method".
        constexpr const char* GAIN_METHOD = "gain";
        constexpr const char* GAIN_AND_FIELD_METHOD = "gain-and-field";

        // Convertit le nom de la méthode en méthode de l'algorithme. Vide si le nom est inconnu.
        std::optional<CorrectionMethod> parseMethod(const std::string& methodName) {
            if (methodName == GAIN_METHOD) return CorrectionMethod::GlobalGain;
            if (methodName == GAIN_AND_FIELD_METHOD) return CorrectionMethod::GainAndField;
            return std::nullopt;
        }

        // Lit puis réduit chaque tuile, dans l'ordre de la liste. Une tuile illisible est une erreur de données :
        // la cause est écrite dans errorMessage (qui reste vide si tout s'est bien passé).
        std::vector<ReducedTile> reduceTiles(const DataNs::TileList& tiles, const BrightnessEqualizerClass& equalizer, std::string& errorMessage) {
            std::vector<ReducedTile> reducedTiles;
            reducedTiles.reserve(tiles.size());
            for (const auto& tile : tiles) {
                const cv::Mat pixels = PhaseCorrelationStepClass::readPixels(*tile, ANALYSIS_LEVEL);
                if (pixels.empty()) {
                    errorMessage = "cannot read " + tile->getSource().string();
                    return {};
                }
                // L'algorithme a besoin de la taille pleine résolution pour ramener les translations à l'échelle réduite.
                const DataNs::Level& fullResolution = tile->getLevel(0);
                reducedTiles.push_back(equalizer.reduceTile(pixels, cv::Size(fullResolution.width, fullResolution.height)));
            }
            return reducedTiles;
        }

        // Convertit les appariements en paires de tuiles voisines : les identifiants de tuiles deviennent des indices
        // dans la liste des tuiles. La translation est déjà en pleine résolution, dans le repère de A.
        // Un identifiant inconnu est une erreur de données : la cause est écrite dans errorMessage.
        std::vector<NeighbourPair> buildNeighbourPairs(const DataNs::TileList& tiles, const std::vector<DataNs::PairMatches>& pairMatches,
            std::string& errorMessage) {
            std::map<int, int> tileIndexById;
            for (size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
                tileIndexById[tiles[tileIndex]->getId()] = static_cast<int>(tileIndex);

            std::vector<NeighbourPair> neighbourPairs;
            for (const DataNs::PairMatches& pair : pairMatches) {
                const auto tileA = tileIndexById.find(pair.tileIdA);
                const auto tileB = tileIndexById.find(pair.tileIdB);
                if (tileA == tileIndexById.end() || tileB == tileIndexById.end()) {
                    const int unknownId = tileA == tileIndexById.end() ? pair.tileIdA : pair.tileIdB;
                    errorMessage = "matches refer to unknown tile id " + std::to_string(unknownId);
                    return {};
                }
                const cv::Point2d translation(pair.featureMatches.translationX, pair.featureMatches.translationY);
                neighbourPairs.push_back({ tileA->second, tileB->second, translation });
            }
            return neighbourPairs;
        }

        // Garde, du résultat de l'algorithme, les gains de la méthode choisie.
        DataNs::IntensityCorrection toCorrection(const EqualizationResult& estimation, CorrectionMethod method) {
            DataNs::IntensityCorrection correction;
            correction.tileGains = method == CorrectionMethod::GlobalGain ? estimation.gainsA : estimation.gainsB;
            correction.fieldStrength = estimation.getFieldStrength(method);
            correction.isolatedTileIndices = estimation.isolatedTiles;
            return correction;
        }

        // Contrôle la méthode puis la présence des entrées. Renvoie la cause de l'échec, vide si tout est en ordre.
        std::string checkInputs(const std::string& methodName, StepNs::DataStoreClass* dataStore) {
            if (!parseMethod(methodName))
                return "method must be '" + std::string(GAIN_METHOD) + "' or '" + GAIN_AND_FIELD_METHOD + "', got '" + methodName + "'";
            if (dataStore == nullptr || !dataStore->has("tiles") || !dataStore->has("matches"))
                return "no tiles or matches: run the import and phase-correlation steps first";
            return {};
        }

        // Étapes : 1. créer l'algorithme ; 2. lire et réduire les tuiles ; 3. convertir les appariements en paires ;
        // 4. estimer. Vide, avec la cause dans errorMessage, au premier échec.
        std::optional<DataNs::IntensityCorrection> estimateCorrection(IntensityCorrectionInput input, StepNs::DataStoreClass& dataStore,
            std::string& errorMessage) {
            // reduction suit le niveau de pyramide lu : la même valeur ne doit pas être écrite à deux endroits.
            input.parameters.reductionFactor = 1 << ANALYSIS_LEVEL;
            // L'algorithme valide ses réglages à la construction (std::invalid_argument si l'un est incohérent).
            std::optional<BrightnessEqualizerClass> equalizer;
            try {
                equalizer.emplace(input.parameters);
            }
            catch (const std::invalid_argument& exception) {
                errorMessage = std::string("invalid parameters: ") + exception.what();
                return std::nullopt;
            }
            const auto& tiles = dataStore.get<DataNs::TileList>("tiles");
            const std::vector<ReducedTile> reducedTiles = reduceTiles(tiles, *equalizer, errorMessage);
            if (!errorMessage.empty()) return std::nullopt;
            const std::vector<NeighbourPair> neighbourPairs =
                buildNeighbourPairs(tiles, dataStore.get<std::vector<DataNs::PairMatches>>("matches"), errorMessage);
            if (!errorMessage.empty()) return std::nullopt;

            const std::optional<EqualizationResult> estimation = equalizer->estimate(reducedTiles, neighbourPairs);
            if (!estimation) {
                errorMessage = equalizer->getFailureReason();
                return std::nullopt;
            }
            return toCorrection(*estimation, *parseMethod(input.method));
        }

    }

    // Déclare l'identité du step : son nom, ses 8 paramètres (valeurs par défaut = celles de l'entrée), les données
    // qu'il lit (tuiles et appariements) et celle qu'il produit (correction). Le pipeline s'en sert pour vérifier
    // l'enchaînement. Le paramètre reduction de l'algorithme n'est pas exposé : il est imposé par ANALYSIS_LEVEL.
    IntensityCorrectionStepClass::IntensityCorrectionStepClass(const IntensityCorrectionInput& initialInput) {
        const auto& algorithmParameters = initialInput.parameters;
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "intensity-correction";
        stepConfig.parameters = { {"string", "method", initialInput.method},
                         {"float", "blurSigma", algorithmParameters.blurSigma},
                         {"int", "borderMargin", algorithmParameters.borderMargin},
                         {"float", "lowThreshold", algorithmParameters.lowThreshold},
                         {"float", "highThreshold", algorithmParameters.highThreshold},
                         {"int", "iterationCount", algorithmParameters.iterationCount},
                         {"float", "huberThreshold", algorithmParameters.huberThreshold},
                         {"float", "gaugeWeight", algorithmParameters.gaugeWeight} };
        stepConfig.inputs = { {"Tiles", "tiles"}, {"PairMatches", "matches"} };
        stepConfig.outputs = { {"IntensityCorrection", "intensityCorrection"} };
        configJson = stepConfig;
    }

    // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut.
    // Un paramètre absent (JSON écrit avant son ajout) garde la valeur par défaut de IntensityCorrectionInput.
    bool IntensityCorrectionStepClass::readInput(IntensityCorrectionInput& input) {
        auto readParameter = [this](const char* parameterName, auto& value) {
            value = getParameter(parameterName).get<std::decay_t<decltype(value)>>();
        };
        try {
            auto& algorithmParameters = input.parameters;
            readParameter("method", input.method);
            readParameter("blurSigma", algorithmParameters.blurSigma);
            readParameter("borderMargin", algorithmParameters.borderMargin);
            readParameter("lowThreshold", algorithmParameters.lowThreshold);
            readParameter("highThreshold", algorithmParameters.highThreshold);
            readParameter("iterationCount", algorithmParameters.iterationCount);
            readParameter("huberThreshold", algorithmParameters.huberThreshold);
            readParameter("gaugeWeight", algorithmParameters.gaugeWeight);
        }
        catch (const std::exception& exception) {   // mauvais type (Json::exception) ou paramètre absent (out_of_range)
            result["error"] = std::string("invalid option: ") + exception.what();
            return false;
        }
        return true;
    }

    // Étapes : 1. lire et contrôler les paramètres et les entrées ; 2. estimer la correction ; 3. la publier.
    bool IntensityCorrectionStepClass::execute() {
        output = {};
        result = Json::object();

        IntensityCorrectionInput input;
        if (!readInput(input)) return false;
        const std::string inputError = checkInputs(input.method, dataStore);
        if (!inputError.empty()) {
            result["error"] = inputError;
            return false;
        }

        std::string errorMessage;
        const std::optional<DataNs::IntensityCorrection> correction = estimateCorrection(input, *dataStore, errorMessage);
        if (!correction) {
            result["error"] = errorMessage;
            return false;
        }

        output.correction = *correction;
        result["method"] = input.method;
        result["tileCount"] = dataStore->get<DataNs::TileList>("tiles").size();
        result["pairCount"] = dataStore->get<std::vector<DataNs::PairMatches>>("matches").size();
        result["isolatedTiles"] = output.correction.isolatedTileIndices;
        dataStore->replace("intensityCorrection", output.correction);   // pour l'assemblage final
        return true;
    }

}
