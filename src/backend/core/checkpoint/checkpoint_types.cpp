// src/backend/core/checkpoint/checkpoint_types.cpp
#include "core/checkpoint/checkpoint_types.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "core/data/mosaic_files.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/types.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CheckpointNs {

    using Json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        // Écrit `value` dans <directory>/<key>.json : une sortie du DataStoreClass = un fichier, nommé d'après sa clé.
        void writeJson(const fs::path& directory, const std::string& key, const Json& value) {
            std::ofstream output(directory / (key + ".json"), std::ios::binary);
            output << value.dump();
            if (!output) throw std::runtime_error("cannot write " + (directory / (key + ".json")).string());
        }

        // Lit <directory>/<key>.json. Lève std::runtime_error s'il est absent.
        Json readJson(const fs::path& directory, const std::string& key) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("missing " + (directory / (key + ".json")).string());
            return Json::parse(input);
        }

        // Les pixels restent dans le dossier de la mosaïque, comme pour les tuiles.
        void saveMosaicFiles(
            const StepNs::DataStoreClass& store,
            const std::string& key,
            const fs::path& directory
        ) {
            const auto& mosaic = store.get<DataNs::MosaicFiles>(key);
            writeJson(directory, key, {
                {"mosaicPath", mosaic.mosaicPath},
                {"totalBlocks", mosaic.totalBlocks}
                });
        }

        void loadMosaicFiles(
            StepNs::DataStoreClass& store,
            const std::string& key,
            const fs::path& directory
        ) {
            const Json data = readJson(directory, key);
            const DataNs::MosaicFiles mosaic{
                data.at("mosaicPath").get<std::string>(),
                data.at("totalBlocks").get<int>()
            };

            if (mosaic.totalBlocks <= 0 || !fs::is_directory(mosaic.mosaicPath)) {
                throw std::runtime_error(
                    "mosaic checkpoint: missing or invalid mosaic directory");
            }

            store.replace(key, mosaic);
        }

        // Un point devient le tableau JSON [x, y].
        Json point(const cv::Point2f& coordinates) { return {coordinates.x, coordinates.y}; }
        Json point(const cv::Point2d& coordinates) { return {coordinates.x, coordinates.y}; }
        Json point(const cv::Point& coordinates) { return {coordinates.x, coordinates.y}; }

        // Une ligne brisée devient un tableau de points.
        Json polyline(const std::vector<cv::Point>& vertices) {
            Json verticesJson = Json::array();
            for (const auto& vertex : vertices) verticesJson.push_back(point(vertex));
            return verticesJson;
        }

        // Inverse de polyline.
        std::vector<cv::Point> readPolyline(const Json& verticesJson) {
            std::vector<cv::Point> vertices;
            for (const Json& vertexJson : verticesJson) vertices.emplace_back(vertexJson.at(0).get<int>(), vertexJson.at(1).get<int>());
            return vertices;
        }

        // Direction -> texte du JSON.
        const char* directionName(DataNs::Direction direction) {
            switch (direction) {
            case DataNs::Direction::Top: return "top";
            case DataNs::Direction::Bottom: return "bottom";
            case DataNs::Direction::Left: return "left";
            case DataNs::Direction::Right: return "right";
            }
            return "right";
        }

        // Texte du JSON -> Direction. Lève std::runtime_error pour un texte inconnu.
        DataNs::Direction readDirection(const std::string& name) {
            if (name == "top") return DataNs::Direction::Top;
            if (name == "bottom") return DataNs::Direction::Bottom;
            if (name == "left") return DataNs::Direction::Left;
            if (name == "right") return DataNs::Direction::Right;
            throw std::runtime_error("unknown direction '" + name + "'");
        }

        // Identifiant d'une voisine pour le JSON, ou -1 si elle n'existe pas.
        int neighbourId(const DataNs::TileClass* neighbour) { return neighbour ? neighbour->getId() : -1; }

        // Sauvegarde du type "Tiles" (TileList), une entrée par tuile : { id, source, tiff, width, height, type, left, right, top, bottom }.
        // Seules les fiches sont écrites, jamais les pixels. width/height sont ceux de la pleine résolution (les autres
        // niveaux se recalculent au chargement) ; les voisines sont écrites par identifiant.
        void saveTiles(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            Json tilesJson = Json::array();
            for (const auto& tile : dataStore.get<DataNs::TileList>(key)) {
                const DataNs::Level& fullResolutionLevel = tile->getLevel(0);
                tilesJson.push_back({{"id", tile->getId()}, {"source", tile->getSource().u8string()}, {"tiff", tile->getTiff().u8string()},
                                    {"width", fullResolutionLevel.width}, {"height", fullResolutionLevel.height}, {"type", tile->getType()},
                                    {"left", neighbourId(tile->getLeft())}, {"right", neighbourId(tile->getRight())},
                                    {"top", neighbourId(tile->getTop())}, {"bottom", neighbourId(tile->getBottom())}});
            }
            writeJson(directory, key, tilesJson);
        }

        // Chargement du type "Tiles". Deux passes : création de toutes les tuiles, puis liaison des voisines (qui peuvent venir
        // après dans la liste).
        void loadTiles(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            const Json tilesJson = readJson(directory, key);
            DataNs::TileList tiles;
            std::map<int, DataNs::TileClass*> tileById;
            for (const Json& tileJson : tilesJson) {
                const fs::path tiffPath = fs::u8path(tileJson.at("tiff").get<std::string>());
                // Les pixels ne sont pas recopiés dans le checkpoint : le TIFF pyramidal doit toujours exister.
                if (!tiffPath.empty() && !fs::exists(tiffPath)) throw std::runtime_error("missing pyramidal TIFF " + tiffPath.string());
                tiles.push_back(std::make_shared<DataNs::TileClass>(tileJson.at("id").get<int>(), fs::u8path(tileJson.at("source").get<std::string>()),
                                                                    tiffPath, tileJson.at("width").get<int>(), tileJson.at("height").get<int>(),
                                                                    tileJson.at("type").get<int>()));
                tileById[tiles.back()->getId()] = tiles.back().get();
            }
            // Deuxième passe : identifiant JSON -> pointeur vers la tuile (-1 ou inconnu -> pas de voisine).
            const auto findTile = [&](const Json& tileIdJson) -> DataNs::TileClass* {
                const auto entry = tileById.find(tileIdJson.get<int>());
                return entry == tileById.end() ? nullptr : entry->second;
            };
            for (size_t index = 0; index < tiles.size(); ++index) {
                const Json& tileJson = tilesJson.at(index);
                tiles[index]->setNeighbours(findTile(tileJson.at("left")), findTile(tileJson.at("right")), findTile(tileJson.at("top")), findTile(tileJson.at("bottom")));
            }
            dataStore.replace(key, std::move(tiles));
        }

        // Sauvegarde du type "PairMatches" (std::vector<PairMatches>), une entrée par paire : identifiants des tuiles, direction,
        // points appariés (pointsInA[i] <-> pointsInB[i]), scores de confiance et translation estimée.
        void savePairMatches(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            Json pairsJson = Json::array();
            for (const DataNs::PairMatches& pair : dataStore.get<std::vector<DataNs::PairMatches>>(key)) {
                Json pointsInAJson = Json::array(), pointsInBJson = Json::array();
                for (const auto& matchPoint : pair.featureMatches.pointsInA) pointsInAJson.push_back(point(matchPoint));
                for (const auto& matchPoint : pair.featureMatches.pointsInB) pointsInBJson.push_back(point(matchPoint));
                pairsJson.push_back({{"tileIdA", pair.tileIdA}, {"tileIdB", pair.tileIdB}, {"direction", directionName(pair.direction)},
                                    {"pointsInA", pointsInAJson}, {"pointsInB", pointsInBJson}, {"confidenceScores", pair.featureMatches.confidenceScores},
                                    {"translationX", pair.featureMatches.translationX}, {"translationY", pair.featureMatches.translationY}});
            }
            writeJson(directory, key, pairsJson);
        }

        // Chargement du type "PairMatches" : relit les champs écrits par savePairMatches.
        void loadPairMatches(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            std::vector<DataNs::PairMatches> pairs;
            for (const Json& pairJson : readJson(directory, key)) {
                DataNs::PairMatches pair;
                pair.tileIdA = pairJson.at("tileIdA").get<int>();
                pair.tileIdB = pairJson.at("tileIdB").get<int>();
                pair.direction = readDirection(pairJson.at("direction").get<std::string>());
                for (const Json& pointJson : pairJson.at("pointsInA")) pair.featureMatches.pointsInA.emplace_back(pointJson.at(0).get<float>(), pointJson.at(1).get<float>());
                for (const Json& pointJson : pairJson.at("pointsInB")) pair.featureMatches.pointsInB.emplace_back(pointJson.at(0).get<float>(), pointJson.at(1).get<float>());
                pair.featureMatches.confidenceScores = pairJson.at("confidenceScores").get<std::vector<float>>();
                pair.featureMatches.translationX = pairJson.at("translationX").get<float>();
                pair.featureMatches.translationY = pairJson.at("translationY").get<float>();
                pairs.push_back(std::move(pair));
            }
            dataStore.replace(key, std::move(pairs));
        }

        // Sauvegarde du type "Positions" (std::vector<TilePositionData>), une entrée par tuile placée : { tileId, position: [x, y], rotationRadians }.
        void savePositions(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            Json positionsJson = Json::array();
            for (const DataNs::TilePositionData& tilePosition : dataStore.get<std::vector<DataNs::TilePositionData>>(key))
                positionsJson.push_back({{"tileId", tilePosition.tileId}, {"position", point(tilePosition.position)}, {"rotationRadians", tilePosition.rotationRadians}});
            writeJson(directory, key, positionsJson);
        }

        // Chargement du type "Positions" : relit les champs écrits par savePositions.
        void loadPositions(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            std::vector<DataNs::TilePositionData> positions;
            for (const Json& positionJson : readJson(directory, key))
                positions.push_back({positionJson.at("tileId").get<std::string>(),
                                    cv::Point2d(positionJson.at("position").at(0).get<double>(), positionJson.at("position").at(1).get<double>()),
                                    positionJson.at("rotationRadians").get<double>()});
            dataStore.replace(key, std::move(positions));
        }

        // Sauvegarde des chemins de couture dans le repère de la mosaïque.
        void saveSeamlines(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            Json seamlinesJson = Json::array();
            for (const DataNs::Seamline& seamline : dataStore.get<DataNs::Seamlines>(key)) {
                Json pointsJson = Json::array();
                for (const cv::Point2d& seamPoint : seamline.points) pointsJson.push_back(point(seamPoint));
                seamlinesJson.push_back({{"tileIdA", seamline.tileIdA}, {"tileIdB", seamline.tileIdB},
                                         {"direction", directionName(seamline.direction)}, {"points", pointsJson}});
            }
            writeJson(directory, key, seamlinesJson);
        }

        void loadSeamlines(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            DataNs::Seamlines seamlines;
            for (const Json& seamlineJson : readJson(directory, key)) {
                DataNs::Seamline seamline;
                seamline.tileIdA = seamlineJson.at("tileIdA").get<int>();
                seamline.tileIdB = seamlineJson.at("tileIdB").get<int>();
                seamline.direction = readDirection(seamlineJson.at("direction").get<std::string>());
                for (const Json& pointJson : seamlineJson.at("points"))
                    seamline.points.emplace_back(pointJson.at(0).get<double>(), pointJson.at(1).get<double>());
                seamlines.push_back(std::move(seamline));
            }
            dataStore.replace(key, std::move(seamlines));
        }

        // Sauvegarde du type "Territories" (std::vector<TileTerritory>), une entrée par tuile : { tileIndex, polygons, holes }.
        // polygons est une liste de lignes brisées ; holes[i] liste les trous du polygone i (même indexation que TileTerritory).
        void saveTerritories(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            Json territoriesJson = Json::array();
            for (const DataNs::TileTerritory& territory : dataStore.get<std::vector<DataNs::TileTerritory>>(key)) {
                Json polygonsJson = Json::array(), holesJson = Json::array();
                for (const auto& polygon : territory.polygons) polygonsJson.push_back(polyline(polygon));
                for (const auto& polygonHoles : territory.holes) {
                    Json polygonHolesJson = Json::array();
                    for (const auto& hole : polygonHoles) polygonHolesJson.push_back(polyline(hole));
                    holesJson.push_back(polygonHolesJson);
                }
                territoriesJson.push_back({{"tileIndex", territory.tileIndex}, {"polygons", polygonsJson}, {"holes", holesJson}});
            }
            writeJson(directory, key, territoriesJson);
        }

        // Chargement du type "Territories" : relit les champs écrits par saveTerritories.
        void loadTerritories(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            std::vector<DataNs::TileTerritory> territories;
            for (const Json& territoryJson : readJson(directory, key)) {
                DataNs::TileTerritory territory;
                territory.tileIndex = territoryJson.at("tileIndex").get<int>();
                for (const Json& polygonJson : territoryJson.at("polygons")) territory.polygons.push_back(readPolyline(polygonJson));
                for (const Json& polygonHolesJson : territoryJson.at("holes")) {
                    std::vector<std::vector<cv::Point>> polygonHoles;
                    for (const Json& holeJson : polygonHolesJson) polygonHoles.push_back(readPolyline(holeJson));
                    territory.holes.push_back(std::move(polygonHoles));
                }
                territories.push_back(std::move(territory));
            }
            dataStore.replace(key, std::move(territories));
        }

        // Sauvegarde du type "IntensityCorrection" : { tileGains, fieldStrength, isolatedTileIndices }.
        void saveIntensityCorrection(const StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            const auto& correction = dataStore.get<DataNs::IntensityCorrection>(key);
            writeJson(directory, key, { {"tileGains", correction.tileGains}, {"fieldStrength", correction.fieldStrength},
                                       {"isolatedTileIndices", correction.isolatedTileIndices} });
        }

        // Chargement du type "IntensityCorrection" : relit les champs écrits par saveIntensityCorrection.
        void loadIntensityCorrection(StepNs::DataStoreClass& dataStore, const std::string& key, const fs::path& directory) {
            const Json correctionJson = readJson(directory, key);
            DataNs::IntensityCorrection correction;
            correction.tileGains = correctionJson.at("tileGains").get<std::vector<double>>();
            correction.fieldStrength = correctionJson.at("fieldStrength").get<double>();
            correction.isolatedTileIndices = correctionJson.at("isolatedTileIndices").get<std::vector<int>>();
            dataStore.replace(key, std::move(correction));
        }
    }

    // Un type de donnée du pipeline -> sa paire (sauvegarde, chargement).
    std::map<std::string, std::pair<CheckpointClass::Saver, CheckpointClass::Loader>> pipelineTypes() {
        return {
            {"Tiles", {saveTiles, loadTiles}},
            {"PairMatches", {savePairMatches, loadPairMatches}},
            {"Positions", {savePositions, loadPositions}},
            {"Seamlines", {saveSeamlines, loadSeamlines}},
            {"Territories", {saveTerritories, loadTerritories}},
            {"IntensityCorrection", {saveIntensityCorrection, loadIntensityCorrection}},
            {"MosaicFiles", {saveMosaicFiles, loadMosaicFiles}},
        };
    }

}
