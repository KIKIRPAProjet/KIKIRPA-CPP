// src/backend/steps/manual_matches_step.cpp
#include "steps/manual_matches_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        // Correction d'une paire, lue dans le paramètre "corrections".
        struct PairCorrection {
            int tileIdA = 0;
            int tileIdB = 0;
            std::vector<cv::Point2f> pointsInA;
            std::vector<cv::Point2f> pointsInB;
        };

        // Lit une liste de points [[x, y], ...]. `path` sert au message d'erreur.
        std::vector<cv::Point2f> readPoints(const Json& pointsJson, const std::string& path) {
            if (!pointsJson.is_array() || pointsJson.empty()) throw std::invalid_argument(path + " must be a non-empty array of [x, y]");
            std::vector<cv::Point2f> points;
            for (const Json& pointJson : pointsJson) {
                const bool isPoint = pointJson.is_array() && pointJson.size() == 2 && pointJson.at(0).is_number() && pointJson.at(1).is_number();
                if (!isPoint) throw std::invalid_argument(path + ": each point must be [x, y], got " + pointJson.dump());
                const cv::Point2f point(pointJson.at(0).get<float>(), pointJson.at(1).get<float>());
                if (!std::isfinite(point.x) || !std::isfinite(point.y)) throw std::invalid_argument(path + ": non-finite point " + pointJson.dump());
                points.push_back(point);
            }
            return points;
        }

        // Lit et vérifie une correction : identifiants entiers, deux listes de points non vides et de même taille.
        PairCorrection readCorrection(const Json& correctionJson, const std::string& path) {
            const bool hasIds = correctionJson.is_object() && correctionJson.contains("tileIdA") && correctionJson.contains("tileIdB") &&
                                correctionJson.at("tileIdA").is_number_integer() && correctionJson.at("tileIdB").is_number_integer();
            if (!hasIds) throw std::invalid_argument(path + " must have integer 'tileIdA' and 'tileIdB'");
            PairCorrection correction;
            correction.tileIdA = correctionJson.at("tileIdA").get<int>();
            correction.tileIdB = correctionJson.at("tileIdB").get<int>();
            correction.pointsInA = readPoints(correctionJson.value("pointsInA", Json()), path + ".pointsInA");
            correction.pointsInB = readPoints(correctionJson.value("pointsInB", Json()), path + ".pointsInB");
            if (correction.pointsInA.size() != correction.pointsInB.size()) {
                std::string message = path;
                message.append(": 'pointsInA' and 'pointsInB' must have the same size, got ")
                    .append(std::to_string(correction.pointsInA.size())).append(" and ").append(std::to_string(correction.pointsInB.size()));
                throw std::invalid_argument(message);
            }
            return correction;
        }

        // Médiane d'une liste non vide. Médiane plutôt que moyenne : un point mal placé ne déplace pas la tuile.
        float computeMedian(std::vector<float> values) {
            const std::size_t middle = values.size() / 2;
            const auto middleIterator = values.begin() + static_cast<std::ptrdiff_t>(middle);
            std::nth_element(values.begin(), middleIterator, values.end());
            if (values.size() % 2 == 1) return values[middle];
            // Taille paire : moyenne des deux valeurs centrales ; la plus petite est le maximum de la moitié basse.
            const float lower = *std::max_element(values.begin(), middleIterator);
            return (lower + values[middle]) / 2;
        }

        // Remplace les points de la paire et recalcule sa translation. Même convention que les matchers :
        // translation = pointsInA - pointsInB (position de B dans le repère de A).
        void applyCorrection(DataNs::PairMatches& pair, PairCorrection correction) {
            std::vector<float> offsetsX;
            std::vector<float> offsetsY;
            for (std::size_t index = 0; index < correction.pointsInA.size(); ++index) {
                offsetsX.push_back(correction.pointsInA[index].x - correction.pointsInB[index].x);
                offsetsY.push_back(correction.pointsInA[index].y - correction.pointsInB[index].y);
            }
            pair.featureMatches.translationX = computeMedian(offsetsX);
            pair.featureMatches.translationY = computeMedian(offsetsY);
            // Points vérifiés par l'utilisateur : confiance maximale.
            pair.featureMatches.confidenceScores.assign(correction.pointsInA.size(), 1.F);
            pair.featureMatches.pointsInA = std::move(correction.pointsInA);
            pair.featureMatches.pointsInB = std::move(correction.pointsInB);
        }

        // "(idA, idB)", pour les messages d'erreur.
        std::string describePair(int tileIdA, int tileIdB) {
            std::string name = "(";
            name.append(std::to_string(tileIdA)).append(", ").append(std::to_string(tileIdB)).append(")");
            return name;
        }

        // Tuile d'identifiant `tileId`, ou nullptr si `tiles` ne la contient pas.
        const DataNs::TileClass* findTile(const DataNs::TileList& tiles, int tileId) {
            const auto tile = std::find_if(tiles.begin(), tiles.end(), [tileId](const std::shared_ptr<DataNs::TileClass>& candidate) {
                return candidate != nullptr && candidate->getId() == tileId;
            });
            return tile == tiles.end() ? nullptr : tile->get();
        }

        // Direction de B par rapport à A quand B est la voisine de droite ou du bas de A, seules directions de PairMatches.
        std::optional<DataNs::Direction> neighbourDirection(const DataNs::TileClass& tileA, const DataNs::TileClass& tileB) {
            if (tileA.getRight() == &tileB) return DataNs::Direction::Right;
            if (tileA.getBottom() == &tileB) return DataNs::Direction::Bottom;
            return std::nullopt;
        }

        // Met la correction dans le sens des paires de matches (B à droite ou en dessous de A) et renvoie la direction
        // de B. Une correction saisie dans l'autre sens est retournée : A et B échangés, avec leurs points.
        DataNs::Direction orientCorrection(PairCorrection& correction, const DataNs::TileList& tiles, const std::string& path) {
            const std::string pairName = describePair(correction.tileIdA, correction.tileIdB);
            const DataNs::TileClass* tileA = findTile(tiles, correction.tileIdA);
            const DataNs::TileClass* tileB = findTile(tiles, correction.tileIdB);
            if (tileA == nullptr || tileB == nullptr) throw std::invalid_argument(path + ": pair " + pairName + " references an unknown tile id");
            if (const auto direction = neighbourDirection(*tileA, *tileB)) return *direction;
            const auto reversedDirection = neighbourDirection(*tileB, *tileA);
            if (!reversedDirection) throw std::invalid_argument(path + ": tiles of pair " + pairName + " are not neighbours");
            std::swap(correction.tileIdA, correction.tileIdB);
            std::swap(correction.pointsInA, correction.pointsInB);
            return *reversedDirection;
        }

        // Paire (A, B) de `pairs`. Absente quand l'appariement ou vote-2d l'a écartée : elle est alors ajoutée à la fin,
        // c'est le cas où la tuile risque de n'être reliée à aucune autre. `added` dit lequel des deux cas s'est produit.
        DataNs::PairMatches& findOrAddPair(std::vector<DataNs::PairMatches>& pairs, const PairCorrection& correction,
                                           DataNs::Direction direction, bool& added) {
            const auto pair = std::find_if(pairs.begin(), pairs.end(), [&correction](const DataNs::PairMatches& candidate) {
                return candidate.tileIdA == correction.tileIdA && candidate.tileIdB == correction.tileIdB;
            });
            added = pair == pairs.end();
            if (!added) return *pair;
            DataNs::PairMatches newPair;
            newPair.tileIdA = correction.tileIdA;
            newPair.tileIdB = correction.tileIdB;
            newPair.direction = direction;
            return pairs.emplace_back(std::move(newPair));
        }

        // Applique toutes les corrections à `pairs` et renvoie les {idA, idB, added} corrigés. Lève std::invalid_argument
        // pour une correction mal formée, une tuile inconnue, deux tuiles non voisines ou une paire corrigée deux fois.
        Json applyCorrections(std::vector<DataNs::PairMatches>& pairs, const DataNs::TileList& tiles, const Json& corrections) {
            if (!corrections.is_array()) throw std::invalid_argument("manual-matches: 'corrections' must be an array");
            Json correctedPairs = Json::array();
            std::set<std::pair<int, int>> correctedIds;   // pour refuser deux corrections de la même paire, dans un sens ou l'autre
            for (std::size_t index = 0; index < corrections.size(); ++index) {
                std::string path = "manual-matches: corrections[";
                path.append(std::to_string(index)).append("]");
                PairCorrection correction = readCorrection(corrections.at(index), path);
                const DataNs::Direction direction = orientCorrection(correction, tiles, path);
                if (!correctedIds.insert({correction.tileIdA, correction.tileIdB}).second)
                    throw std::invalid_argument(path + ": pair " + describePair(correction.tileIdA, correction.tileIdB) + " is corrected twice");
                bool added = false;
                DataNs::PairMatches& pair = findOrAddPair(pairs, correction, direction, added);
                correctedPairs.push_back({{"idA", correction.tileIdA}, {"idB", correction.tileIdB}, {"added", added}});
                applyCorrection(pair, std::move(correction));
            }
            return correctedPairs;
        }

    }

    ManualMatchesStepClass::ManualMatchesStepClass(const ManualMatchesInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "manual-matches";
        stepConfig.parameters = {{"json", "corrections", input.corrections}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"PairMatches", "matches"}};
        stepConfig.outputs = {{"PairMatches", "matches"}};
        configJson = stepConfig;
    }

    bool ManualMatchesStepClass::execute() {
        output = {};
        result = {{"pairs", 0}, {"correctedPairs", Json::array()}, {"error", ""}};
        try {
            if (dataStore == nullptr) throw std::invalid_argument("manual-matches: DataStore is not attached");
            // Travail sur une copie : en cas d'erreur, matches reste tel que l'appariement l'a produit.
            std::vector<DataNs::PairMatches> pairs = dataStore->get<std::vector<DataNs::PairMatches>>("matches");
            const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
            result["correctedPairs"] = applyCorrections(pairs, tiles, getParameter("corrections"));
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
