// src/backend/steps/vote_2d_step.cpp
#include "steps/vote_2d_step.h"

#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        constexpr int MINIMUM_POINT_COUNT = 2;
        //filtre les paires de correspondances par Vote2D
        void filterPairs(const std::vector<DataNs::PairMatches>& pairs,
                         RejectionNs::VoteRejectorClass& rejector,
                         std::vector<DataNs::PairMatches>& keptPairs,
                         Json& failures) {
            for (const DataNs::PairMatches& pair : pairs) {
                DataNs::PairMatches filteredPair = pair;
                DataNs::Alignment alignment;
                const auto& scores = filteredPair.featureMatches.confidenceScores;
                const bool hasValidScores = scores.empty() || scores.size() == filteredPair.featureMatches.pointsInA.size();

                //Si les correspondances sont suffisantes et que le filtrage est réussi, on garde la paire, sinon on l'ajoute aux échecs.
                if (hasValidScores && rejector.filterPairMatches(filteredPair, alignment)) { 
                    keptPairs.push_back(std::move(filteredPair));
                } else {
                    failures.push_back({{"idA", pair.tileIdA},
                                        {"idB", pair.tileIdB},
                                        {"reason", "insufficient valid correspondences or inliers"}});
                }
            }
        }

    }

    // Configuration par défaut : nom du step, paramètres avec leurs valeurs par défaut, entrées et sorties.
    Vote2DStepClass::Vote2DStepClass(const Vote2DInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "vote-2d";
        stepConfig.parameters = {{"float", "seuil", input.seuil},
                                 {"float", "case", input.cellSize},
                                 {"int", "minMatches", input.minMatches},
                                 {"int", "minInliers", input.minInliers}};
        stepConfig.inputs = {{"PairMatches", "matches"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }
    //Extraction des paramètres du JSON pour le step avec vérification de leur validité.
    Vote2DInput Vote2DStepClass::readInput() const {
        Vote2DInput input;
        const auto readCount = [this](const char* name) {
            const auto value = getParameter(name);
            if (value < MINIMUM_POINT_COUNT || value > std::numeric_limits<int>::max())
                throw std::invalid_argument(std::string("vote-2d: ") + name + " must be at least 2");
            return value.get<int>();
        };
        input.seuil = getParameter("seuil").get<float>();
        input.cellSize = getParameter("case").get<float>();
        input.minMatches = readCount("minMatches");
        input.minInliers = readCount("minInliers");
        if (!std::isfinite(input.seuil) || input.seuil <= 0.f)
            throw std::invalid_argument("vote-2d: seuil must be finite and positive");
        if (!std::isfinite(input.cellSize) || input.cellSize <= 0.f)
            throw std::invalid_argument("vote-2d: case must be finite and positive");
        return input;
    }

    bool Vote2DStepClass::execute() {
        output.value.clear();
        result = {{"pairs", 0}, {"kept", 0}, {"removed", 0}, {"failures", Json::array()}, {"error", ""}};
        try {
            if (!dataStore) throw std::invalid_argument("vote-2d: DataStore is not attached");
            const Vote2DInput input = readInput();
            const auto& pairs = dataStore->get<std::vector<DataNs::PairMatches>>("matches");
            RejectionNs::VoteRejectorClass rejector(Json{{"seuil", input.seuil},
                                                         {"case", input.cellSize},
                                                         {"minMatches", input.minMatches},
                                                         {"minInliers", input.minInliers}});
            filterPairs(pairs, rejector, output.value, result["failures"]);
            result["pairs"] = output.value.size();
            result["kept"] = output.value.size();
            result["removed"] = pairs.size() - output.value.size();
            if (output.value.empty()) throw std::runtime_error("vote-2d: no pair has enough inliers");

            // En dernier : `pairs` désigne l'ancienne donnée, qui n'existe plus après replace.
            dataStore->replace(output.key, output.value);
            return true;
        } catch (const std::exception& exception) {
            output.value.clear();
            result["error"] = exception.what();
            return false;
        }
    }

}
