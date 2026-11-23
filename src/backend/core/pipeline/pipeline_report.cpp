// src/backend/core/pipeline/pipeline_report.cpp
#include "core/pipeline/pipeline_report.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/data/tile.h"
#include "core/io/file_replacement.h"
#include "core/io/unicode_path.h"
#include "core/pipeline/pipeline.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    namespace FilesystemNs = std::filesystem;

    namespace {

        Json serializePoint(const cv::Point2f& coordinates) { return {coordinates.x, coordinates.y}; }
        Json serializePoint(const cv::Point2d& coordinates) { return {coordinates.x, coordinates.y}; }
        Json serializePoint(const cv::Point& coordinates) { return {coordinates.x, coordinates.y}; }

        const char* getDirectionName(DataNs::Direction direction) {
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
                const auto& confidenceScores = pair.featureMatches.confidenceScores;
                double confidenceTotal = 0.0;
                for (const float score : confidenceScores) confidenceTotal += score;
                pairs.push_back({
                    {"tileIdA", pair.tileIdA},
                    {"tileIdB", pair.tileIdB},
                    {"direction", getDirectionName(pair.direction)},
                    {"correspondenceCount", pair.featureMatches.pointsInA.size()},
                    {"confidenceCount", confidenceScores.size()},
                    {"meanConfidence", confidenceScores.empty() ? Json(nullptr)
                        : Json(confidenceTotal / static_cast<double>(confidenceScores.size()))},
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
                    {"position", serializePoint(position.position)},
                    {"rotationRadians", position.rotationRadians}
                });
            return positions;
        }

        Json serializeTerritory(const DataNs::TileTerritory& territory) {
            size_t holeCount = 0;
            size_t vertexCount = 0;
            for (const auto& polygon : territory.polygons) vertexCount += polygon.size();
            for (const auto& polygonHoles : territory.holes) {
                holeCount += polygonHoles.size();
                for (const auto& hole : polygonHoles) vertexCount += hole.size();
            }
            return {
                {"tileIndex", territory.tileIndex},
                {"polygonCount", territory.polygons.size()},
                {"holeCount", holeCount},
                {"vertexCount", vertexCount}
            };
        }

        Json serializeTerritories(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json territories = Json::array();
            for (const DataNs::TileTerritory& territory : dataStore.get<std::vector<DataNs::TileTerritory>>(key))
                territories.push_back(serializeTerritory(territory));
            return territories;
        }

        Json serializeSeamlines(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json seamlines = Json::array();
            for (const DataNs::Seamline& seamline : dataStore.get<DataNs::Seamlines>(key)) {
                seamlines.push_back({
                    {"tileIdA", seamline.tileIdA},
                    {"tileIdB", seamline.tileIdB},
                    {"direction", getDirectionName(seamline.direction)},
                    {"pointCount", seamline.points.size()},
                    {"start", seamline.points.empty() ? Json(nullptr) : serializePoint(seamline.points.front())},
                    {"end", seamline.points.empty() ? Json(nullptr) : serializePoint(seamline.points.back())}
                });
            }
            return seamlines;
        }

        Json serializePairResiduals(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json residuals = Json::array();
            for (const DataNs::PairResidual& residual : dataStore.get<std::vector<DataNs::PairResidual>>(key))
                residuals.push_back({
                    {"tileIdA", residual.tileIdA},
                    {"tileIdB", residual.tileIdB},
                    {"residual", serializePoint(residual.residual)},
                    {"residualNorm", residual.residualNorm}
                });
            return residuals;
        }

        Json serializeOverlapEstimates(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            Json estimates = Json::array();
            for (const DataNs::PairOverlapEstimate& estimate : dataStore.get<DataNs::OverlapEstimates>(key))
                estimates.push_back({
                    {"tileIdA", estimate.tileIdA},
                    {"tileIdB", estimate.tileIdB},
                    {"direction", getDirectionName(estimate.direction)},
                    {"translation", serializePoint(estimate.estimate.translationBInA)},
                    {"overlapFraction", estimate.estimate.overlapFraction},
                    {"confidencePeakRatio", estimate.estimate.peakRatio},
                    {"commonAreaInA", {estimate.commonAreaInA.x, estimate.commonAreaInA.y,
                                       estimate.commonAreaInA.width, estimate.commonAreaInA.height}},
                    {"commonAreaInB", {estimate.commonAreaInB.x, estimate.commonAreaInB.y,
                                       estimate.commonAreaInB.width, estimate.commonAreaInB.height}}
                });
            return estimates;
        }

        Json serializeIntensityCorrection(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            const DataNs::IntensityCorrection& correction = dataStore.get<DataNs::IntensityCorrection>(key);
            return {
                {"tileGains", correction.tileGains},
                {"fieldStrength", correction.fieldStrength},
                {"isolatedTileIndices", correction.isolatedTileIndices}
            };
        }

        Json serializeMosaic(const StepNs::DataStoreClass& dataStore, const std::string& key) {
            const DataNs::Mosaic& mosaic = dataStore.get<DataNs::Mosaic>(key);
            return {
                {"imagePath", mosaic.imagePath.u8string()},
                {"provenancePath", mosaic.provenancePath.u8string()},
                {"maskPath", mosaic.maskPath.u8string()},
                {"width", mosaic.canvasSize.width},
                {"height", mosaic.canvasSize.height}
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
            if (slot.dataType == "Mosaic") return serializeMosaic(dataStore, slot.key);
            if (slot.dataType == "PairResiduals") return serializePairResiduals(dataStore, slot.key);
            if (slot.dataType == "OverlapEstimates") return serializeOverlapEstimates(dataStore, slot.key);
            throw std::runtime_error("report: unsupported data type '" + slot.dataType + "'");
        }

        Json serializeSlots(const std::vector<ConfigNs::DataSlot>& slots,
                            const StepNs::DataStoreClass& dataStore, bool includeValues) {
            Json serializedSlots = Json::array();
            for (const ConfigNs::DataSlot& slot : slots) {
                Json serializedSlot = {
                    {"key", slot.key},
                    {"dataType", slot.dataType}
                };
                if (!includeValues) {
                    serializedSlot["available"] = dataStore.has(slot.key);
                } else {
                    try {
                        serializedSlot["value"] = serializeData(slot, dataStore);
                    } catch (const std::exception& exception) {
                        serializedSlot["value"] = nullptr;
                        serializedSlot["note"] = std::string("Donnée non détaillée dans le rapport : ") +
                                                 exception.what();
                    }
                }
                serializedSlots.push_back(std::move(serializedSlot));
            }
            return serializedSlots;
        }

        std::string summarizePairMatches(const Json& pairs) {
            size_t correspondenceCount = 0;
            for (const Json& pair : pairs)
                correspondenceCount += pair.value("correspondenceCount", size_t{0});
            return "Paires mesurées : " + std::to_string(pairs.size()) +
                " ; correspondances retenues : " + std::to_string(correspondenceCount);
        }

        std::string summarizeOutput(const Json& output) {
            const Json& value = output.at("value");
            const std::string& type = output.at("dataType").get_ref<const std::string&>();
            if (value.is_null()) return output.value("note", std::string("Sortie non détaillée."));
            if (type == "Tiles") return "Tuiles importées : " + std::to_string(value.size());
            if (type == "PairMatches") return summarizePairMatches(value);
            if (type == "Positions") return "Positions calculées : " + std::to_string(value.size());
            if (type == "IntensityCorrection")
                return "Corrections de luminosité calculées pour " +
                    std::to_string(value.at("tileGains").size()) + " tuile(s).";
            if (type == "Territories") return "Territoires calculés : " + std::to_string(value.size());
            if (type == "Seamlines") return "Coutures calculées : " + std::to_string(value.size());
            if (type == "PairResiduals") return "Résidus de positionnement : " + std::to_string(value.size());
            if (type == "OverlapEstimates") return "Recouvrements estimés : " + std::to_string(value.size());
            if (type == "Mosaic")
                return "Mosaïque assemblée : " + std::to_string(value.at("width").get<int>()) + " × " +
                    std::to_string(value.at("height").get<int>()) + " pixels.";
            return {};
        }

        std::string summarizeExport(const Json& result) {
            const Json& image = result.at("image");
            const Json size = result.value("canvasSize", Json::array());
            return "Fichier exporté : " + image.value("path", std::string()) +
                (size.size() == 2 ? " ; dimensions : " + std::to_string(size.at(0).get<int>()) + " × " +
                    std::to_string(size.at(1).get<int>()) + " pixels." : "");
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

        const char* getStatusLabel(const std::string& status) {
            if (status == "completed") return "Réussie";
            if (status == "failed") return "Échec";
            if (status == "skipped") return "Réutilisée sans recalcul";
            if (status == "notReached") return "Non atteinte";
            if (status == "running") return "En cours";
            if (status == "paused") return "Interrompue";
            return "Inconnue";
        }

        void appendHtmlHeader(const Json& metadata, std::string& html) {
            html += "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>Rapport d'assemblage</title>"
                "<style>body{font:16px sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem}"
                "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f4f4f4;padding:1rem}"
                "section{border-top:1px solid #bbb;padding:1rem 0}.failed{color:#a00}</style></head><body>"
                "<h1>Rapport d'assemblage — ";
            html += escapeHtml(metadata.value("projectName", "Projet inconnu"));
            html += "</h1>";
            html += "<p>Exécution : " + escapeHtml(metadata.value("executionId", "inconnue")) + "</p>";
            html += "<p>Date : " + escapeHtml(metadata.value("startedAt", "inconnue")) + "</p>";
            html += "<p>Statut global : <strong>" + escapeHtml(metadata.value("status", "inconnu")) + "</strong></p>";
        }

        void appendHtmlGridDimensions(const Json& grid, std::string& html) {
            if (grid.contains("rows") && grid.contains("columns"))
                html += "<p>Grille : " + std::to_string(grid.at("rows").get<int>()) + " × " +
                    std::to_string(grid.at("columns").get<int>()) + "</p>";
        }

        void appendHtmlTileOrder(const Json& grid, std::string& html) {
            if (!grid.contains("tiles") || !grid.at("tiles").is_array()) return;
            html += "<h2>Ordre d'import configuré</h2><ol>";
            for (const Json& tile : grid.at("tiles")) {
                html += "<li>(" + std::to_string(tile.at("row").get<int>() + 1) + ", " +
                    std::to_string(tile.at("column").get<int>() + 1) + ") — " +
                    escapeHtml(tile.value("path", std::string())) + "</li>";
            }
            html += "</ol>";
        }

        void appendHtmlGrid(const Json& metadata, std::string& html) {
            const Json project = metadata.value("project", Json::object());
            if (!project.is_object() || !project.contains("grid") || !project.at("grid").is_object()) return;
            const Json& grid = project.at("grid");
            appendHtmlGridDimensions(grid, html);
            appendHtmlTileOrder(grid, html);
        }

        void appendHtmlStep(const Json& step, std::string& html) {
            const std::string status = step.value("status", "notReached");
            html += "<section><h2>Étape " + std::to_string(step.at("index").get<size_t>() + 1) +
                " — " + escapeHtml(step.value("name", "inconnue")) + "</h2>";
            html += "<p>Statut : <strong" + std::string(status == "failed" ? " class=\"failed\"" : "") +
                ">" + getStatusLabel(status) + "</strong></p>";
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

        std::string renderHtml(const Json& reportJson) {
            const Json metadata = reportJson.value("metadata", Json::object());
            std::string html;
            appendHtmlHeader(metadata, html);
            appendHtmlGrid(metadata, html);
            for (const Json& step : reportJson.at("steps")) appendHtmlStep(step, html);
            if (reportJson.contains("output") && !reportJson.at("output").is_null())
                html += "<h2>Fichier final</h2><pre>" + escapeHtml(reportJson.at("output").dump(2)) + "</pre>";
            html += "</body></html>\n";
            return html;
        }

        void writeAtomically(const FilesystemNs::path& destination, const std::string& content) {
            if (destination.empty()) throw std::invalid_argument("report path must not be empty");
            if (destination.has_parent_path()) FilesystemNs::create_directories(destination.parent_path());
            const FilesystemNs::path temporary = IONs::makeTemporaryPath(destination);
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("report: cannot create " + IONs::pathToUtf8(temporary));
                output << content;
                output.close();
                if (!output) {
                    FilesystemNs::remove(temporary);
                    throw std::runtime_error("report: cannot write " + IONs::pathToUtf8(temporary));
                }
            }
            std::string errorMessage;
            if (!IONs::replaceFile(temporary, destination, errorMessage)) {
                FilesystemNs::remove(temporary);
                throw std::runtime_error(errorMessage);
            }
        }

    }

    Json createStepInputs(const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore) {
        const ConfigNs::StepConfig config = step.getConfigJson().get<ConfigNs::StepConfig>();
        return serializeSlots(config.inputs, dataStore, false);
    }

    Json createStepReport(size_t stepIndex, const StepNs::StepClass& step,
                          const StepNs::DataStoreClass& dataStore, const Json& inputs) {
        const ConfigNs::StepConfig config = step.getConfigJson().get<ConfigNs::StepConfig>();
        Json outputs = serializeSlots(config.outputs, dataStore, true);
        const Json result = step.getResult();
        Json stepReport = {
            {"index", stepIndex},
            {"name", config.name},
            {"status", "completed"},
            {"reason", nullptr},
            {"configuration", step.getConfigJson()},
            {"inputs", inputs},
            {"outputs", std::move(outputs)},
            {"result", result}
        };
        stepReport["summary"] = makeStepSummary(stepReport);
        return stepReport;
    }

    std::string makeStepSummary(const Json& stepReport) {
        const Json& result = stepReport.at("result");
        if (result.is_object() && result.contains("error")) {
            const Json& error = result.at("error");
            return error.is_string() ? error.get<std::string>() : error.dump();
        }
        for (const Json& output : stepReport.at("outputs")) {
            const std::string summary = summarizeOutput(output);
            if (!summary.empty()) return summary;
        }
        if (stepReport.value("name", std::string()) == "export" &&
            result.is_object() && result.contains("image"))
            return summarizeExport(result);
        return "Étape terminée. Résultat détaillé disponible ci-dessous.";
    }

    void writeReport(const FilesystemNs::path& reportPath, const Json& reportJson) {
        writeAtomically(reportPath, reportJson.dump(2) + '\n');
    }

    void writeReportSummary(const FilesystemNs::path& reportPath, const Json& reportJson) {
        Json summary = {{"metadata", Json::object()}, {"progress", Json::object()}};
        const Json metadata = reportJson.value("metadata", Json::object());
        for (const char* key : {"projectName", "executionId", "startedAt", "status"})
            if (metadata.contains(key)) summary["metadata"][key] = metadata.at(key);
        const Json steps = reportJson.contains("steps") && reportJson.at("steps").is_array()
            ? reportJson.at("steps") : Json::array();
        size_t completedSteps = 0;
        for (const Json& step : steps) {
            const std::string status = step.value("status", std::string());
            if (status == "completed" || status == "skipped") ++completedSteps;
            if (status == "running")
                summary["progress"]["currentStep"] = {
                    {"index", step.value("index", size_t{0})},
                    {"name", step.value("name", std::string())}
                };
        }
        summary["progress"]["completedSteps"] = completedSteps;
        summary["progress"]["totalSteps"] = steps.size();
        writeAtomically(reportPath.parent_path() / "execution-summary.json", summary.dump(2) + '\n');
    }

    void writeHtmlReport(const FilesystemNs::path& reportPath, const Json& reportJson) {
        writeAtomically(reportPath, renderHtml(reportJson));
    }

}
