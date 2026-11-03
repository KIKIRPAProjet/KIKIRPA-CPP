// src/backend/steps/intensity_correction_step.cpp
#include "steps/intensity_correction_step.h"

#include "core/config/pipeline_config.h"
#include "steps/phase_correlation_step.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;
    using stitching::egalisation::Egalisation;
    using stitching::egalisation::Methode;
    using stitching::egalisation::PaireVoisine;
    using stitching::egalisation::Resultat;
    using stitching::egalisation::TuileReduite;

    namespace {

        // Niveau de pyramide lu pour l'estimation : 1/8 de la résolution. Le modèle n'a que (tuiles + 1) inconnues,
        // une image réduite suffit, et une seule image pleine résolution est en mémoire à la fois.
        constexpr int ANALYSIS_LEVEL = 3;

        // Valeurs possibles du paramètre "method".
        constexpr const char* GAIN_METHOD = "gain";
        constexpr const char* GAIN_AND_FIELD_METHOD = "gain-and-field";

        // Convertit le nom de la méthode en méthode de l'algorithme. Vide si le nom est inconnu.
        std::optional<Methode> parseMethod(const std::string& methodName) {
            if (methodName == GAIN_METHOD) return Methode::GainGlobal;
            if (methodName == GAIN_AND_FIELD_METHOD) return Methode::GainEtChamp;
            return std::nullopt;
        }

        // Lit puis réduit chaque tuile, dans l'ordre de la liste. Une tuile illisible est une erreur de données :
        // la cause est écrite dans errorMessage (qui reste vide si tout s'est bien passé).
        std::vector<TuileReduite> reduceTiles(const DataNs::TileList& tiles, const Egalisation& egalisation, std::string& errorMessage) {
            std::vector<TuileReduite> reducedTiles;
            reducedTiles.reserve(tiles.size());
            for (const auto& tile : tiles) {
                const cv::Mat pixels = PhaseCorrelationStepClass::readPixels(*tile, ANALYSIS_LEVEL);
                if (pixels.empty()) {
                    errorMessage = "cannot read " + tile->getSource().string();
                    return {};
                }
                // L'algorithme a besoin de la taille pleine résolution pour ramener les translations à l'échelle réduite.
                const DataNs::Level& fullResolution = tile->getLevel(0);
                reducedTiles.push_back(egalisation.reduire(pixels, cv::Size(fullResolution.width, fullResolution.height)));
            }
            return reducedTiles;
        }

        // Convertit les appariements en paires de tuiles voisines : les identifiants de tuiles deviennent des indices
        // dans la liste des tuiles. La translation est déjà en pleine résolution, dans le repère de A.
        // Un identifiant inconnu est une erreur de données : la cause est écrite dans errorMessage.
        std::vector<PaireVoisine> buildNeighbourPairs(const DataNs::TileList& tiles, const std::vector<DataNs::PairMatches>& pairMatches,
            std::string& errorMessage) {
            std::map<int, int> tileIndexById;
            for (size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
                tileIndexById[tiles[tileIndex]->getId()] = static_cast<int>(tileIndex);

            std::vector<PaireVoisine> neighbourPairs;
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
        DataNs::IntensityCorrection toCorrection(const Resultat& estimation, Methode method) {
            DataNs::IntensityCorrection correction;
            correction.tileGains = method == Methode::GainGlobal ? estimation.gainsA : estimation.gainsB;
            correction.fieldStrength = estimation.champ(method);
            correction.isolatedTileIndices = estimation.tuilesIsolees;
            return correction;
        }

    }

    // Déclare l'identité du step : son nom, ses 8 paramètres (valeurs par défaut = celles de l'entrée), les données
    // qu'il lit (tuiles et appariements) et celle qu'il produit (correction). Le pipeline s'en sert pour vérifier
    // l'enchaînement. Le paramètre reduction de l'algorithme n'est pas exposé : il est imposé par ANALYSIS_LEVEL.
    IntensityCorrectionStepClass::IntensityCorrectionStepClass(const IntensityCorrectionInput& initialInput) {
        const auto& algorithmParameters = initialInput.parametres;
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "intensity-correction";
        stepConfig.parameters = { {"string", "method", initialInput.method},
                                 {"float", "blurSigma", algorithmParameters.flou},
                                 {"int", "borderMargin", algorithmParameters.marge},
                                 {"float", "lowThreshold", algorithmParameters.seuilBas},
                                 {"float", "highThreshold", algorithmParameters.seuilHaut},
                                 {"int", "iterationCount", algorithmParameters.iterations},
                                 {"float", "huberThreshold", algorithmParameters.huber},
                                 {"float", "gaugeWeight", algorithmParameters.poidsJauge} };
        stepConfig.inputs = { {"Tiles", "tiles"}, {"PairMatches", "matches"} };
        stepConfig.outputs = { {"IntensityCorrection", "intensityCorrection"} };
        configJson = stepConfig;
    }

    // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut.
    // Un paramètre absent (JSON écrit avant son ajout) garde la valeur par défaut de IntensityCorrectionInput.
    bool IntensityCorrectionStepClass::readInput(IntensityCorrectionInput& input) {
        auto readParameter = [this](const char* parameterName, auto& value) {
            try {
                value = getParameter(parameterName).get<std::decay_t<decltype(value)>>();
            }
            catch (const std::out_of_range&) {}
            };
        try {
            auto& algorithmParameters = input.parametres;
            readParameter("method", input.method);
            readParameter("blurSigma", algorithmParameters.flou);
            readParameter("borderMargin", algorithmParameters.marge);
            readParameter("lowThreshold", algorithmParameters.seuilBas);
            readParameter("highThreshold", algorithmParameters.seuilHaut);
            readParameter("iterationCount", algorithmParameters.iterations);
            readParameter("huberThreshold", algorithmParameters.huber);
            readParameter("gaugeWeight", algorithmParameters.poidsJauge);
        }
        catch (const Json::exception& exception) {
            result["error"] = std::string("invalid option type: ") + exception.what();
            return false;
        }
        return true;
    }

    // Étapes : 1. lire et contrôler les paramètres et les entrées ; 2. créer l'algorithme ; 3. lire et réduire les
    // tuiles, convertir les appariements en paires ; 4. estimer ; 5. publier la correction.
    bool IntensityCorrectionStepClass::execute() {
        output = {};
        result = Json::object();

        IntensityCorrectionInput input;
        if (!readInput(input)) return false;

        const std::optional<Methode> method = parseMethod(input.method);
        if (!method) {
            result["error"] = "method must be '" + std::string(GAIN_METHOD) + "' or '" + GAIN_AND_FIELD_METHOD + "', got '" + input.method + "'";
            return false;
        }
        if (!dataStore || !dataStore->has("tiles") || !dataStore->has("matches")) {
            result["error"] = "no tiles or matches: run the import and phase-correlation steps first";
            return false;
        }

        // reduction suit le niveau de pyramide lu : la même valeur ne doit pas être écrite à deux endroits.
        input.parametres.reduction = 1 << ANALYSIS_LEVEL;
        // L'algorithme valide ses réglages à la construction (std::invalid_argument si l'un est incohérent).
        std::optional<Egalisation> egalisation;
        try {
            egalisation.emplace(input.parametres);
        }
        catch (const std::invalid_argument& exception) {
            result["error"] = std::string("invalid parameters: ") + exception.what();
            return false;
        }

        const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
        std::string errorMessage;
        const std::vector<TuileReduite> reducedTiles = reduceTiles(tiles, *egalisation, errorMessage);
        if (!errorMessage.empty()) {
            result["error"] = errorMessage;
            return false;
        }
        const std::vector<PaireVoisine> neighbourPairs =
            buildNeighbourPairs(tiles, dataStore->get<std::vector<DataNs::PairMatches>>("matches"), errorMessage);
        if (!errorMessage.empty()) {
            result["error"] = errorMessage;
            return false;
        }

        const std::optional<Resultat> estimation = egalisation->estimer(reducedTiles, neighbourPairs);
        if (!estimation) {
            result["error"] = egalisation->raisonEchec();
            return false;
        }

        output.correction = toCorrection(*estimation, *method);
        result["method"] = input.method;
        result["tileCount"] = tiles.size();
        result["pairCount"] = neighbourPairs.size();
        result["isolatedTiles"] = output.correction.isolatedTileIndices;
        dataStore->replace("intensityCorrection", output.correction);   // pour l'assemblage final
        return true;
    }

}