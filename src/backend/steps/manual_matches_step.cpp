// src/backend/steps/manual_matches_step.cpp
#include "steps/manual_matches_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        // Lit une liste de points [[x, y], ...]. `path` sert au message d'erreur.
        std::vector<cv::Point2f> readPoints(const Json& pointsJson, const std::string& path) {
            if (!pointsJson.is_array() || pointsJson.empty()) throw std::invalid_argument(path + " must be a non-empty array of [x, y]");
            std::vector<cv::Point2f> points;
            for (const Json& pointJson : pointsJson) {
                if (!pointJson.is_array() || pointJson.size() != 2 || !pointJson.at(0).is_number() || !pointJson.at(1).is_number())
                    throw std::invalid_argument(path + ": each point must be [x, y], got " + pointJson.dump());
                const float x = pointJson.at(0).get<float>();
                const float y = pointJson.at(1).get<float>();
                if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument(path + ": non-finite point " + pointJson.dump());
                points.emplace_back(x, y);
            }
            return points;
        }

        // Médiane d'une liste non vide. Médiane plutôt que moyenne : un point mal placé ne déplace pas la tuile.
        float computeMedian(std::vector<float> values) {
            const std::size_t middle = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
            if (values.size() % 2 == 1) return values[middle];
            const float upper = values[middle];
            const float lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
            return (lower + upper) / 2.f;
        }

        // Remplace les points de la paire et recalcule sa translation. Même convention que les matchers :
        // translation = pointsInA - pointsInB (position de B dans le repère de A).
        void applyCorrection(DataNs::PairMatches& pair, std::vector<cv::Point2f> pointsInA, std::vector<cv::Point2f> pointsInB) {
            std::vector<float> offsetsX, offsetsY;
            for (std::size_t index = 0; index < pointsInA.size(); ++index) {
                offsetsX.push_back(pointsInA[index].x - pointsInB[index].x);
                offsetsY.push_back(pointsInA[index].y - pointsInB[index].y);
            }
            pair.featureMatches.translationX = computeMedian(offsetsX);
            pair.featureMatches.translationY = computeMedian(offsetsY);
            // Points vérifiés par l'utilisateur : confiance maximale.
            pair.featureMatches.confidenceScores.assign(pointsInA.size(), 1.f);
            pair.featureMatches.pointsInA = std::move(pointsInA);
            pair.featureMatches.pointsInB = std::move(pointsInB);
        }

    }

    ManualMatchesStepClass::ManualMatchesStepClass(const ManualMatchesInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "manual-matches";
        stepConfig.parameters = {{"json", "corrections", input.corrections}};
        stepConfig.inputs = {{"PairMatches", "matches"}};
        stepConfig.outputs = {{"PairMatches", "matches"}};
        configJson = stepConfig;
    }

    bool ManualMatchesStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"correctedPairs", Json::array()}, {"error", ""}};
        try {
            if (!dataStore) throw std::invalid_argument("manual-matches: DataStore is not attached");
            const Json corrections = getParameter("corrections");
            if (!corrections.is_array()) throw std::invalid_argument("manual-matches: 'corrections' must be an array");

            // Travail sur une copie : en cas d'erreur, matches reste tel que l'appariement l'a produit.
            std::vector<DataNs::PairMatches> pairs = dataStore->get<std::vector<DataNs::PairMatches>>("matches");
            std::set<std::pair<int, int>> correctedIds;   // pour refuser deux corrections de la même paire
            for (std::size_t index = 0; index < corrections.size(); ++index) {
                const Json& correction = corrections.at(index);
                const std::string path = "manual-matches: corrections[" + std::to_string(index) + "]";
                if (!correction.is_object() || !correction.contains("tileIdA") || !correction.contains("tileIdB") ||
                    !correction.at("tileIdA").is_number_integer() || !correction.at("tileIdB").is_number_integer())
                    throw std::invalid_argument(path + " must have integer 'tileIdA' and 'tileIdB'");
                const int tileIdA = correction.at("tileIdA").get<int>();
                const int tileIdB = correction.at("tileIdB").get<int>();
                const std::string pairName = "(" + std::to_string(tileIdA) + ", " + std::to_string(tileIdB) + ")";
                if (!correctedIds.insert({tileIdA, tileIdB}).second) throw std::invalid_argument(path + ": pair " + pairName + " is corrected twice");

                // L'ordre A, B est celui de matches : il fixe le sens de la translation.
                const auto pair = std::find_if(pairs.begin(), pairs.end(), [&](const DataNs::PairMatches& candidate) {
                    return candidate.tileIdA == tileIdA && candidate.tileIdB == tileIdB;
                });
                if (pair == pairs.end()) throw std::invalid_argument(path + ": pair " + pairName + " is not in 'matches'");

                std::vector<cv::Point2f> pointsInA = readPoints(correction.value("pointsInA", Json()), path + ".pointsInA");
                std::vector<cv::Point2f> pointsInB = readPoints(correction.value("pointsInB", Json()), path + ".pointsInB");
                if (pointsInA.size() != pointsInB.size())
                    throw std::invalid_argument(path + ": 'pointsInA' and 'pointsInB' must have the same size, got " +
                                                std::to_string(pointsInA.size()) + " and " + std::to_string(pointsInB.size()));
                applyCorrection(*pair, std::move(pointsInA), std::move(pointsInB));
                result["correctedPairs"].push_back({{"idA", tileIdA}, {"idB", tileIdB}});
            }

            output.value = std::move(pairs);
            dataStore->replace(output.key, output.value);
            result["pairs"] = output.value.size();
            return true;
        } catch (const std::exception& exception) {
            result["correctedPairs"] = Json::array();
            result["error"] = exception.what();
            return false;
        }
    }

}
