// src/backend/steps/absolute_pos_step.cpp
#include "steps/absolute_pos_step.h"

#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "algorithms/stitching/positioning/position_solver.h"
#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/types.hpp>
#include <string>
#include <vector>
#include <limits>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        struct TileIndex {
            std::map<int, std::size_t> indexById;
            std::vector<std::string> names;
            std::vector<int> ids;   // ids[indice] = TileClass::getId()
        };

        TileIndex indexTiles(const DataNs::TileList& tiles) {
            if (tiles.empty()) throw std::invalid_argument("absolute-pos: no tiles");
            TileIndex index;
            std::set<std::string> names;
            for (auto& tile : tiles) {
                if (!tile) throw std::invalid_argument("absolute-pos: null tile");
                if (!index.indexById.emplace(tile->getId(), index.names.size()).second)
                    throw std::invalid_argument("absolute-pos: duplicate tile id");
                int row = 0, column = 0;
                std::string name;
                if (!GraphCutNs::ImagePretreatmentClass::parseTileName(tile->getSource().string(), row, column, name))
                    throw std::invalid_argument("absolute-pos: tile filename must end in _r<row>_c<column>.tif[f]");
                if (!names.insert(name).second) throw std::invalid_argument("absolute-pos: duplicate tile grid position");
                index.names.push_back(std::move(name));
                index.ids.push_back(tile->getId());
            }
            return index;
        }

        std::vector<PositioningNs::RelativeTilePosition> collectOffsets(const std::vector<DataNs::PairMatches>& pairs,
                                                                       const TileIndex& index, Json& failures) {
            std::vector<PositioningNs::RelativeTilePosition> offsets;
            for (auto& pair : pairs) {
                auto first = index.indexById.find(pair.tileIdA);
                auto second = index.indexById.find(pair.tileIdB);
                std::string reason;
                if (first == index.indexById.end() || second == index.indexById.end()) reason = "unknown tile id";
                else if (first == second) reason = "a pair must reference two different tiles";
                else if (!std::isfinite(pair.featureMatches.translationX) || !std::isfinite(pair.featureMatches.translationY))
                    reason = "non-finite translation";
                if (!reason.empty()) {
                    failures.push_back({{"idA", pair.tileIdA}, {"idB", pair.tileIdB}, {"reason", reason}});
                    continue;
                }
                offsets.push_back({first->second, second->second,
                                   {pair.featureMatches.translationX, pair.featureMatches.translationY}, 0.0});
            }
            return offsets;
        }

        int readReferenceId(const Json& value) {
            if ((value.is_number_unsigned() && value.get<unsigned long long>() > static_cast<unsigned long long>(std::numeric_limits<int>::max())) ||
                (!value.is_number_unsigned() && (value.get<long long>() < std::numeric_limits<int>::min() || value.get<long long>() > std::numeric_limits<int>::max())))
                throw std::invalid_argument("absolute-pos: referenceTileId is outside the supported integer range");
            return value.get<int>();
        }

        std::size_t selectReference(const TileIndex& index, int tileId) {
            if (tileId == -1) return index.indexById.begin()->second;
            auto entry = index.indexById.find(tileId);
            if (entry == index.indexById.end()) throw std::invalid_argument("absolute-pos: unknown referenceTileId");
            return entry->second;
        }

        std::vector<DataNs::TilePositionData> makePositions(const TileIndex& index, const PositioningNs::PositionSolution& solution) {
            if (std::find(solution.connected.begin(), solution.connected.end(), false) != solution.connected.end())
                throw std::invalid_argument("absolute-pos: some tiles are disconnected from the reference");
            std::vector<DataNs::TilePositionData> positions;
            for (std::size_t tileIndex = 0; tileIndex < index.names.size(); ++tileIndex)
                positions.push_back({index.names[tileIndex], solution.positions[tileIndex], solution.rotations[tileIndex]});
            return positions;
        }

        // Écart de chaque paire après ajustement : translation mesurée − (position B − position A) ajustées, en pixels.
        std::vector<DataNs::PairResidual> computeResiduals(const TileIndex& index,
                                                           const std::vector<PositioningNs::RelativeTilePosition>& offsets,
                                                           const PositioningNs::PositionSolution& solution) {
            std::vector<DataNs::PairResidual> residuals;
            residuals.reserve(offsets.size());
            for (const auto& offset : offsets) {
                const cv::Point2d adjusted = solution.positions[offset.tileIndexB] - solution.positions[offset.tileIndexA];
                const cv::Point2d residual = offset.translation - adjusted;
                residuals.push_back({index.ids[offset.tileIndexA], index.ids[offset.tileIndexB], residual,
                                     std::hypot(residual.x, residual.y)});
            }
            return residuals;
        }

        // Checkpoint du type "PairResiduals" : <clé>.json, une entrée par paire { tileIdA, tileIdB, residual: [rx, ry], residualNorm }.
        void savePairResiduals(const StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            Json residualsJson = Json::array();
            for (const DataNs::PairResidual& pairResidual : store.get<std::vector<DataNs::PairResidual>>(key))
                residualsJson.push_back({{"tileIdA", pairResidual.tileIdA}, {"tileIdB", pairResidual.tileIdB},
                                         {"residual", Json::array({pairResidual.residual.x, pairResidual.residual.y})},
                                         {"residualNorm", pairResidual.residualNorm}});
            std::ofstream output(directory / (key + ".json"), std::ios::binary | std::ios::trunc);
            output << residualsJson.dump(2);
            output.close();
            if (!output) throw std::runtime_error("absolute-pos checkpoint: cannot write " + key);
        }

        // Relit le fichier écrit par savePairResiduals.
        void loadPairResiduals(StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("absolute-pos checkpoint: cannot read " + key);
            std::vector<DataNs::PairResidual> residuals;
            for (const Json& residualJson : Json::parse(input))
                residuals.push_back({residualJson.at("tileIdA").get<int>(), residualJson.at("tileIdB").get<int>(),
                                     cv::Point2d(residualJson.at("residual").at(0).get<double>(),
                                                 residualJson.at("residual").at(1).get<double>()),
                                     residualJson.at("residualNorm").get<double>()});
            store.replace(key, std::move(residuals));
        }

    }

    AbsolutePosStepClass::AbsolutePosStepClass(const AbsolutePosInput& input) {
        // Sérialisation de pairResiduals pour les checkpoints, déclarée une seule fois (comme intensity-correction).
        if (!CheckpointNs::CheckpointClass::hasType("PairResiduals"))
            CheckpointNs::CheckpointClass::registerType("PairResiduals", savePairResiduals, loadPairResiduals);
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "absolute-pos";
        stepConfig.parameters = {{"int", "referenceTileId", input.referenceTileId}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"PairMatches", "matches"}};
        stepConfig.outputs = {{"Positions", "positions"}, {"PairResiduals", "pairResiduals"}};
        configJson = stepConfig;
    }

    bool AbsolutePosStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"positions", 0}, {"failures", Json::array()}, {"error", ""}};
        try {
            if (!dataStore) throw std::invalid_argument("absolute-pos: DataStore is not attached");
            dataStore->replace(output.key, std::vector<DataNs::TilePositionData>{});
            dataStore->replace(output.residualsKey, std::vector<DataNs::PairResidual>{});
            auto index = indexTiles(dataStore->get<DataNs::TileList>("tiles"));
            auto referenceIndex = selectReference(index, readReferenceId(getParameter("referenceTileId")));
            auto offsets = collectOffsets(dataStore->get<std::vector<DataNs::PairMatches>>("matches"), index, result["failures"]);
            result["pairs"] = offsets.size();
            auto solution = PositioningNs::solveRelativePositions(index.names.size(), offsets, referenceIndex);
            output.value = makePositions(index, solution);
            output.residuals = computeResiduals(index, offsets, solution);
            dataStore->replace(output.key, output.value);
            dataStore->replace(output.residualsKey, output.residuals);
            result["positions"] = output.value.size();
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

}
