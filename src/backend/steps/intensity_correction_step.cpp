// src/backend/steps/intensity_correction_step.cpp
#include "steps/intensity_correction_step.h"

#include "algorithms/brightness_equalization/brightness_equalizer.h"
#include "algorithms/brightness_equalization/brightness_equalization_types.h"
#include "core/config/pipeline_config.h"
#include "core/checkpoint/intensity_correction_checkpoint.h"
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        constexpr int ANALYSIS_REDUCTION = 8;
        using EqualizerClass = BrightnessEqualizationNs::BrightnessEqualizerClass;
        using ReducedTile = BrightnessEqualizationNs::ReducedTile;
        using NeighbourPair = BrightnessEqualizationNs::NeighbourPair;
        using Estimation = BrightnessEqualizationNs::EqualizationResult;
        using Method = BrightnessEqualizationNs::CorrectionMethod;

        Method parseMethod(const std::string& name) {
            if (name == "gain") return Method::GlobalGain;
            if (name == "gain-and-field") return Method::GainAndField;
            throw std::invalid_argument("intensity-correction: method must be 'gain' or 'gain-and-field'");
        }

        int readInteger(const Json& value) {
            if ((value.is_number_unsigned() && value.get<unsigned long long>() > static_cast<unsigned long long>(std::numeric_limits<int>::max())) ||
                (!value.is_number_unsigned() && (value.get<long long>() < std::numeric_limits<int>::min() || value.get<long long>() > std::numeric_limits<int>::max())))
                throw std::invalid_argument("intensity-correction: integer parameter is outside the supported range");
            return value.get<int>();
        }

        std::map<int, int> indexTiles(const DataNs::TileList& tiles) {
            if (tiles.size() < 2 || tiles.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("intensity-correction: at least two tiles are required, within the supported index range");
            std::map<int, int> indexById;
            for (std::size_t index = 0; index < tiles.size(); ++index) {
                if (!tiles[index]) throw std::invalid_argument("intensity-correction: null tile");
                if (!indexById.emplace(tiles[index]->getId(), static_cast<int>(index)).second)
                    throw std::invalid_argument("intensity-correction: duplicate tile id");
            }
            return indexById;
        }

        std::vector<ReducedTile> reduceTiles(const DataNs::TileList& tiles, const EqualizerClass& algorithm) {
            std::vector<ReducedTile> reduced;
            reduced.reserve(tiles.size());
            for (auto& tile : tiles) {
                // Une seule image pleine résolution à la fois ; l'algorithme préserve la dynamique 8/16 bits.
                auto pixels = cv::imread(tile->getSource().string(), cv::IMREAD_UNCHANGED);
                if (pixels.empty()) throw std::invalid_argument("intensity-correction: cannot read " + tile->getSource().string());
                auto level = tile->getLevel(0);
                if (pixels.cols != level.width || pixels.rows != level.height || pixels.type() != tile->getType())
                    throw std::invalid_argument("intensity-correction: tile pixels do not match imported metadata");
                reduced.push_back(algorithm.reduceTile(pixels));
            }
            return reduced;
        }

        std::vector<NeighbourPair> buildPairs(const std::map<int, int>& indexById,
                                              const std::vector<DataNs::PairMatches>& matches, Json& failures) {
            std::vector<NeighbourPair> pairs;
            std::set<std::pair<int, int>> seen;
            for (auto& match : matches) {
                auto first = indexById.find(match.tileIdA), second = indexById.find(match.tileIdB);
                auto translation = cv::Point2d(match.featureMatches.translationX, match.featureMatches.translationY);
                std::string reason;
                if (first == indexById.end() || second == indexById.end()) reason = "unknown tile id";
                else if (first == second) reason = "a pair must reference two different tiles";
                else if (!std::isfinite(translation.x) || !std::isfinite(translation.y)) reason = "non-finite translation";
                else if (!seen.emplace(std::min(match.tileIdA, match.tileIdB), std::max(match.tileIdA, match.tileIdB)).second)
                    reason = "duplicate tile pair";
                if (!reason.empty()) {
                    failures.push_back({{"idA", match.tileIdA}, {"idB", match.tileIdB}, {"reason", reason}});
                    continue;
                }
                pairs.push_back({first->second, second->second, translation});
            }
            if (pairs.empty()) throw std::invalid_argument("intensity-correction: no usable tile pairs");
            return pairs;
        }

        void validateParameters(const BrightnessEqualizationNs::Parameters& parameters) {
            for (auto value : {parameters.blurSigma, parameters.lowThreshold, parameters.highThreshold, parameters.huberThreshold, parameters.gaugeWeight})
                if (!std::isfinite(value)) throw std::invalid_argument("intensity-correction: parameters must be finite");
        }

        EqualizerClass createAlgorithm(const BrightnessEqualizationNs::Parameters& parameters) {
            try {
                return EqualizerClass(parameters);
            } catch (const std::invalid_argument&) {
                throw std::invalid_argument("intensity-correction: invalid algorithm parameters");
            }
        }

        DataNs::IntensityCorrection makeCorrection(const Estimation& estimation, Method method) {
            DataNs::IntensityCorrection correction;
            correction.tileGains = method == Method::GlobalGain ? estimation.gainsA : estimation.gainsB;
            correction.fieldStrength = estimation.getFieldStrength(method);
            correction.isolatedTileIndices = estimation.isolatedTiles;
            if (!std::isfinite(correction.fieldStrength)) throw std::runtime_error("intensity-correction: non-finite illumination field");
            for (auto gain : correction.tileGains)
                if (!std::isfinite(gain) || gain <= 0.0) throw std::runtime_error("intensity-correction: gains must be finite and positive");
            return correction;
        }

        void recordFailedPair(const NeighbourPair& pair, const DataNs::TileList& tiles, Json& result) {
            result["failures"].push_back({{"idA", tiles.at(static_cast<std::size_t>(pair.tileIndexA))->getId()},
                                         {"idB", tiles.at(static_cast<std::size_t>(pair.tileIndexB))->getId()},
                                         {"reason", "no valid overlap samples"}});
        }

        Estimation estimateTiles(EqualizerClass& algorithm, const DataNs::TileList& tiles,
                                  const std::vector<NeighbourPair>& pairs, Json& result) {
            auto estimation = algorithm.estimate(reduceTiles(tiles, algorithm), pairs);
            if (!estimation) {
                for (auto& pair : pairs) recordFailedPair(pair, tiles, result);
                throw std::runtime_error("intensity-correction: estimation failed: " + algorithm.getFailureReason());
            }
            for (auto pairIndex : estimation->emptyPairs)
                recordFailedPair(pairs.at(static_cast<std::size_t>(pairIndex)), tiles, result);
            result["pairs"] = pairs.size() - estimation->emptyPairs.size();
            result["samples"] = estimation->sampleCount;
            result["isolatedTiles"] = estimation->isolatedTiles;
            return *estimation;
        }

    }

    IntensityCorrectionStepClass::IntensityCorrectionStepClass(const IntensityCorrectionInput& input) {
        CheckpointNs::registerIntensityCorrectionCheckpoint();
        auto& parameters = input.algorithmParameters;
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "intensity-correction";
        stepConfig.parameters = {{"string", "method", input.method}, {"float", "blurSigma", parameters.blurSigma},
                                 {"int", "borderMargin", parameters.borderMargin}, {"float", "lowThreshold", parameters.lowThreshold},
                                 {"float", "highThreshold", parameters.highThreshold}, {"int", "iterationCount", parameters.iterationCount},
                                 {"float", "huberThreshold", parameters.huberThreshold}, {"float", "gaugeWeight", parameters.gaugeWeight}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"PairMatches", "matches"}};
        stepConfig.outputs = {{"IntensityCorrection", "intensityCorrection"}};
        configJson = stepConfig;
    }

    IntensityCorrectionInput IntensityCorrectionStepClass::readInput() const {
        IntensityCorrectionInput input;
        input.method = getParameter("method").get<std::string>();
        auto& parameters = input.algorithmParameters;
        parameters.reductionFactor = ANALYSIS_REDUCTION;
        parameters.blurSigma = getParameter("blurSigma").get<double>();
        parameters.borderMargin = readInteger(getParameter("borderMargin"));
        parameters.lowThreshold = getParameter("lowThreshold").get<double>();
        parameters.highThreshold = getParameter("highThreshold").get<double>();
        parameters.iterationCount = readInteger(getParameter("iterationCount"));
        parameters.huberThreshold = getParameter("huberThreshold").get<double>();
        parameters.gaugeWeight = getParameter("gaugeWeight").get<double>();
        validateParameters(parameters);
        return input;
    }

    bool IntensityCorrectionStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"failures", Json::array()}, {"error", ""}};
        try {
            if (!dataStore) throw std::invalid_argument("intensity-correction: DataStore is not attached");
            dataStore->replace(output.key, DataNs::IntensityCorrection{});
            auto input = readInput();
            auto method = parseMethod(input.method);
            auto algorithm = createAlgorithm(input.algorithmParameters);
            auto& tiles = dataStore->get<DataNs::TileList>("tiles");
            auto pairs = buildPairs(indexTiles(tiles), dataStore->get<std::vector<DataNs::PairMatches>>("matches"), result["failures"]);
            auto estimation = estimateTiles(algorithm, tiles, pairs, result);
            output.value = makeCorrection(estimation, method);
            dataStore->replace(output.key, output.value);
            result["method"] = input.method;
            result["tileCount"] = tiles.size();
            return true;
        } catch (const std::exception& exception) {
            output.value = {};
            result["error"] = exception.what();
            return false;
        }
    }

}
