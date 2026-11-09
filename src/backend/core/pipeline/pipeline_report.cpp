// src/backend/core/pipeline/pipeline_report.cpp
#include "core/pipeline/pipeline_report.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PipelineNs {

    namespace fs = std::filesystem;

    namespace {

        Json point(const cv::Point2f& coordinates) { return {coordinates.x, coordinates.y}; }
        Json point(const cv::Point2d& coordinates) { return {coordinates.x, coordinates.y}; }
        Json point(const cv::Point& coordinates) { return {coordinates.x, coordinates.y}; }

        const char* directionName(DataNs::Direction direction) {
            switch (direction) {
            case DataNs::Direction::Top: return "top";
            case DataNs::Direction::Bottom: return "bottom";
            case DataNs::Direction::Left: return "left";
            case DataNs::Direction::Right: return "right";
            }
            throw std::invalid_argument("report: unknown direction");
        }

        Json serializeTiles(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json tiles = Json::array();
            for (const auto& tile : dataStore.get<DataNs::TileList>(key)) {
                const DataNs::Level& level = tile->getLevel(0);
                const auto neighbourId = [](const DataNs::TileClass* neighbour) {
                    return neighbour ? Json(neighbour->getId()) : Json(nullptr);
                };
                tiles.push_back({
                    {"id", tile->getId()},
                    {"source", tile->getSource().u8string()},
                    {"pyramidalTiff", tile->getTiff().u8string()},
                    {"width", level.width},
                    {"height", level.height},
                    {"openCvType", tile->getType()},
                    {"neighbours", {
                        {"left", neighbourId(tile->getLeft())},
                        {"right", neighbourId(tile->getRight())},
                        {"top", neighbourId(tile->getTop())},
                        {"bottom", neighbourId(tile->getBottom())}
                    }}
                });
            }
            return tiles;
        }

        Json serializeMatches(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json pairs = Json::array();
            for (const DataNs::PairMatches& pair : dataStore.get<std::vector<DataNs::PairMatches>>(key)) {
                Json pointsInA = Json::array();
                Json pointsInB = Json::array();
                for (const cv::Point2f& matchPoint : pair.featureMatches.pointsInA)
                    pointsInA.push_back(point(matchPoint));
                for (const cv::Point2f& matchPoint : pair.featureMatches.pointsInB)
                    pointsInB.push_back(point(matchPoint));
                pairs.push_back({
                    {"tileIdA", pair.tileIdA},
                    {"tileIdB", pair.tileIdB},
                    {"direction", directionName(pair.direction)},
                    {"pointsInA", std::move(pointsInA)},
                    {"pointsInB", std::move(pointsInB)},
                    {"confidenceScores", pair.featureMatches.confidenceScores},
                    {"translationX", pair.featureMatches.translationX},
                    {"translationY", pair.featureMatches.translationY}
                });
            }
            return pairs;
        }

        Json serializePositions(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json positions = Json::array();
            for (const DataNs::TilePositionData& position : dataStore.get<std::vector<DataNs::TilePositionData>>(key))
                positions.push_back({
                    {"tileId", position.tileId},
                    {"position", point(position.position)},
                    {"rotationRadians", position.rotationRadians}
                });
            return positions;
        }

        Json serializeTerritories(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json territories = Json::array();
            for (const DataNs::TileTerritory& territory : dataStore.get<std::vector<DataNs::TileTerritory>>(key)) {
                Json polygons = Json::array();
                Json holes = Json::array();
                for (const auto& polygon : territory.polygons) {
                    Json points = Json::array();
                    for (const cv::Point& vertex : polygon) points.push_back(point(vertex));
                    polygons.push_back(std::move(points));
                }
                for (const auto& polygonHoles : territory.holes) {
                    Json holesForPolygon = Json::array();
                    for (const auto& hole : polygonHoles) {
                        Json points = Json::array();
                        for (const cv::Point& vertex : hole) points.push_back(point(vertex));
                        holesForPolygon.push_back(std::move(points));
                    }
                    holes.push_back(std::move(holesForPolygon));
                }
                territories.push_back({
                    {"tileIndex", territory.tileIndex},
                    {"polygons", std::move(polygons)},
                    {"holes", std::move(holes)}
                });
            }
            return territories;
        }

        Json serializeSeamlines(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json seamlines = Json::array();
            for (const DataNs::Seamline& seamline : dataStore.get<DataNs::Seamlines>(key)) {
                Json points = Json::array();
                for (const cv::Point2d& seamPoint : seamline.points) points.push_back(point(seamPoint));
                seamlines.push_back({
                    {"tileIdA", seamline.tileIdA},
                    {"tileIdB", seamline.tileIdB},
                    {"direction", directionName(seamline.direction)},
                    {"points", std::move(points)}
                });
            }
            return seamlines;
        }

        Json serializeIntensityCorrection(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            const DataNs::IntensityCorrection& correction = dataStore.get<DataNs::IntensityCorrection>(key);
            return {
                {"tileGains", correction.tileGains},
                {"fieldStrength", correction.fieldStrength},
                {"isolatedTileIndices", correction.isolatedTileIndices}
            };
        }

        Json serializeCompositeBlocks(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            const DataNs::CompositeBlockList& blocks = dataStore.get<DataNs::CompositeBlockList>(key);
            int width = 0;
            int height = 0;
            Json blockRectangles = Json::array();
            for (const DataNs::CompositeBlock& block : blocks) {
                width = std::max(width, block.blockRect.x + block.blockRect.width);
                height = std::max(height, block.blockRect.y + block.blockRect.height);
                blockRectangles.push_back({
                    block.blockRect.x, block.blockRect.y,
                    block.blockRect.width, block.blockRect.height
                });
            }
            return {
                {"blockCount", blocks.size()},
                {"width", width},
                {"height", height},
                {"blockRectangles", std::move(blockRectangles)}
            };
        }

        Json serializeData(const ConfigNs::DataSlot& slot, const StepNs::DataStoreClass& dataStore) {
            if (!dataStore.has(slot.key))
                throw std::runtime_error("report: missing data '" + slot.key + "'");
            if (slot.dataType == "Tiles") return serializeTiles(dataStore, slot.key);
            if (slot.dataType == "PairMatches") return serializeMatches(dataStore, slot.key);
            if (slot.dataType == "Positions") return serializePositions(dataStore, slot.key);
            if (slot.dataType == "Territories") return serializeTerritories(dataStore, slot.key);
            if (slot.dataType == "Seamlines") return serializeSeamlines(dataStore, slot.key);
            if (slot.dataType == "IntensityCorrection") return serializeIntensityCorrection(dataStore, slot.key);
            if (slot.dataType == "CompositeBlocks") return serializeCompositeBlocks(dataStore, slot.key);
            throw std::runtime_error("report: unsupported data type '" + slot.dataType + "'");
        }

        Json serializeSlots(const std::vector<ConfigNs::DataSlot>& slots,
                            const StepNs::DataStoreClass& dataStore) {
            Json serializedSlots = Json::array();
            for (const ConfigNs::DataSlot& slot : slots) {
                serializedSlots.push_back({
                    {"key", slot.key},
                    {"dataType", slot.dataType},
                    {"value", serializeData(slot, dataStore)}
                });
            }
            return serializedSlots;
        }

        std::string escapeHtml(const std::string& text) {
            std::string escaped;
            escaped.reserve(text.size());
            for (const char character : text) {
                switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                case '\'': escaped += "&#39;"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        const char* statusLabel(const std::string& status) {
            if (status == "completed") return "Réussie";
            if (status == "failed") return "Échec";
            if (status == "skipped") return "Réutilisée sans recalcul";
            if (status == "notReached") return "Non atteinte";
            if (status == "running") return "En cours";
            if (status == "paused") return "Interrompue";
            return "Inconnue";
        }

        std::string renderHtml(const Json& reportJson) {
            const Json metadata = reportJson.value("metadata", Json::object());
            std::string html = "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>Rapport d'assemblage</title>"
                "<style>body{font:16px sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem}"
                "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f4f4f4;padding:1rem}"
                "section{border-top:1px solid #bbb;padding:1rem 0}.failed{color:#a00}</style></head><body>"
                "<h1>Rapport d'assemblage — " +
                escapeHtml(metadata.value("projectName", "Projet inconnu")) + "</h1>";
            html += "<p>Exécution : " + escapeHtml(metadata.value("executionId", "inconnue")) + "</p>";
            html += "<p>Date : " + escapeHtml(metadata.value("startedAt", "inconnue")) + "</p>";
            html += "<p>Statut global : <strong>" + escapeHtml(metadata.value("status", "inconnu")) + "</strong></p>";
            const Json project = metadata.value("project", Json::object());
            if (project.is_object() && project.contains("grid") && project.at("grid").is_object()) {
                const Json& grid = project.at("grid");
                if (grid.contains("rows") && grid.contains("columns"))
                    html += "<p>Grille : " + std::to_string(grid.at("rows").get<int>()) + " × " +
                        std::to_string(grid.at("columns").get<int>()) + "</p>";
                if (grid.contains("tiles") && grid.at("tiles").is_array()) {
                    html += "<h2>Ordre d'import configuré</h2><ol>";
                    for (const Json& tile : grid.at("tiles")) {
                        html += "<li>(" + std::to_string(tile.at("row").get<int>() + 1) + ", " +
                            std::to_string(tile.at("column").get<int>() + 1) + ") — " +
                            escapeHtml(tile.value("path", std::string())) + "</li>";
                    }
                    html += "</ol>";
                }
            }
            for (const Json& step : reportJson.at("steps")) {
                const std::string status = step.value("status", "notReached");
                html += "<section><h2>Étape " + std::to_string(step.at("index").get<size_t>() + 1) +
                    " — " + escapeHtml(step.value("name", "inconnue")) + "</h2>";
                html += "<p>Statut : <strong" + std::string(status == "failed" ? " class=\"failed\"" : "") +
                    ">" + statusLabel(status) + "</strong></p>";
                html += "<p>" + escapeHtml(step.value("summary", "")) + "</p>";
                if (step.contains("reason") && !step.at("reason").is_null())
                    html += "<p>Motif : " + escapeHtml(step.at("reason").get<std::string>()) + "</p>";
                if (step.contains("inputs") && !step.at("inputs").empty())
                    html += "<h3>Entrées</h3><pre>" + escapeHtml(step.at("inputs").dump(2)) + "</pre>";
                if (step.contains("outputs") && !step.at("outputs").empty())
                    html += "<h3>Sorties</h3><pre>" + escapeHtml(step.at("outputs").dump(2)) + "</pre>";
                if (step.contains("result") && !step.at("result").is_null())
                    html += "<h3>Résultat</h3><pre>" + escapeHtml(step.at("result").dump(2)) + "</pre>";
                html += "</section>";
            }
            if (reportJson.contains("output") && !reportJson.at("output").is_null())
                html += "<h2>Fichier final</h2><pre>" + escapeHtml(reportJson.at("output").dump(2)) + "</pre>";
            html += "</body></html>\n";
            return html;
        }

        fs::path temporaryPath(const fs::path& destination) {
            fs::path temporary = destination;
            temporary += ".tmp";
            return temporary;
        }

        void replaceFile(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "cannot replace " + destination.string());
#else
            std::error_code errorCode;
            fs::rename(temporary, destination, errorCode);
            if (errorCode) throw std::system_error(errorCode, "cannot replace " + destination.string());
#endif
        }

        void writeAtomically(const fs::path& destination, const std::string& content) {
            if (destination.empty()) throw std::invalid_argument("report path must not be empty");
            if (destination.has_parent_path()) fs::create_directories(destination.parent_path());
            const fs::path temporary = temporaryPath(destination);
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("report: cannot create " + temporary.string());
                output << content;
                output.close();
                if (!output) {
                    fs::remove(temporary);
                    throw std::runtime_error("report: cannot write " + temporary.string());
                }
            }
            try {
                replaceFile(temporary, destination);
            } catch (...) {
                fs::remove(temporary);
                throw;
            }
        }

    }

    Json createStepInputs(const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore) {
        const ConfigNs::StepConfig config = step.getConfigJson().get<ConfigNs::StepConfig>();
        return serializeSlots(config.inputs, dataStore);
    }

    Json createStepReport(size_t stepIndex, const StepNs::StepClass& step,
                          const StepNs::DataStoreClass& dataStore, const Json& inputs) {
        const ConfigNs::StepConfig config = step.getConfigJson().get<ConfigNs::StepConfig>();
        Json outputs = serializeSlots(config.outputs, dataStore);
        const Json result = step.getResult();
        return {
            {"index", stepIndex},
            {"name", config.name},
            {"status", "completed"},
            {"summary", makeStepSummary(config.name, outputs, result)},
            {"reason", nullptr},
            {"configuration", step.getConfigJson()},
            {"parameters", step.getConfigJson().value("parameters", Json::array())},
            {"inputs", inputs},
            {"outputs", std::move(outputs)},
            {"result", result}
        };
    }

    std::string makeStepSummary(const std::string& stepName, const Json& outputs, const Json& result) {
        if (result.is_object() && result.contains("error")) {
            const Json& error = result.at("error");
            return error.is_string() ? error.get<std::string>() : error.dump();
        }
        for (const Json& output : outputs) {
            const Json& value = output.at("value");
            const std::string& type = output.at("dataType").get_ref<const std::string&>();
            if (type == "Tiles") return "Tuiles importées : " + std::to_string(value.size());
            if (type == "PairMatches") {
                size_t correspondenceCount = 0;
                for (const Json& pair : value) correspondenceCount += pair.at("pointsInA").size();
                return "Paires mesurées : " + std::to_string(value.size()) +
                    " ; correspondances retenues : " + std::to_string(correspondenceCount);
            }
            if (type == "Positions") return "Positions calculées : " + std::to_string(value.size());
            if (type == "IntensityCorrection")
                return "Corrections de luminosité calculées pour " +
                    std::to_string(value.at("tileGains").size()) + " tuile(s).";
            if (type == "Territories") return "Territoires calculés : " + std::to_string(value.size());
            if (type == "Seamlines") return "Coutures calculées : " + std::to_string(value.size());
            if (type == "CompositeBlocks")
                return "Blocs assemblés : " + std::to_string(value.at("blockCount").get<size_t>()) +
                    " ; dimensions : " + std::to_string(value.at("width").get<int>()) + " × " +
                    std::to_string(value.at("height").get<int>()) + " pixels.";
        }
        if (stepName == "export" && result.is_object() && result.contains("image")) {
            const Json& image = result.at("image");
            const Json size = result.value("canvasSize", Json::array());
            return "Fichier exporté : " + image.value("path", std::string()) +
                (size.size() == 2 ? " ; dimensions : " + std::to_string(size.at(0).get<int>()) + " × " +
                    std::to_string(size.at(1).get<int>()) + " pixels." : "");
        }
        return "Étape terminée. Résultat détaillé disponible ci-dessous.";
    }

    void writeReport(const fs::path& reportPath, const Json& reportJson) {
        writeAtomically(reportPath, reportJson.dump(2) + '\n');
    }

    void writeHtmlReport(const fs::path& reportPath, const Json& reportJson) {
        writeAtomically(reportPath, renderHtml(reportJson));
    }

}
