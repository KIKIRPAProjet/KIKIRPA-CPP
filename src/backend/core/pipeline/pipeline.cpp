// src/backend/core/pipeline/pipeline.cpp
#include "core/pipeline/pipeline.h"

#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/pipeline/pipeline_report.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PipelineNs {

    namespace fs = std::filesystem;

    namespace {

        // Nom du step lu dans sa config JSON ; chaîne vide si le step n'a pas de config.
        std::string stepName(const StepNs::StepClass& step) {
            return step.getConfigJson().is_object() ? step.getConfigJson().value("name", "") : "";
        }

        // Envoie le message au logger s'il existe ; sinon, seuls les messages d'erreur vont sur std::cerr.
        void report(const PipelineClass::Logger& log, const std::string& message, bool isError) {
            if (log) log(message);
            else if (isError) std::cerr << message << '\n';
        }

        // Index du step où démarrer :
        //  - validStepCount = nombre de steps de tête déjà faits (runState.completedSteps), de même nom, dont le checkpoint
        //    est complet et à jour. Dès qu'un step est invalide, lui et tous les suivants sont à refaire ;
        //  - startFromStep permet de revenir plus tôt (refaire un step), jamais d'aller au-delà de validStepCount
        //    (les données des steps précédents manqueraient).
        size_t resolveStart(const std::vector<std::unique_ptr<StepNs::StepClass>>& steps, const RunState& runState,
                            const CheckpointNs::CheckpointClass& checkpoint, const PipelineClass::Logger& log) {
            // On avance tant que le step d'index validStepCount est déjà fait ET a un checkpoint valide ; on s'arrête au premier qui ne l'est pas.
            size_t validStepCount = 0;
            while (validStepCount < runState.completedSteps.size() && validStepCount < steps.size() &&
                runState.completedSteps[validStepCount] == stepName(*steps[validStepCount]) && checkpoint.valid(validStepCount, *steps[validStepCount]))
                ++validStepCount;
            if (checkpoint.enabled() && validStepCount < runState.completedSteps.size())
                report(log, "checkpoint of step " + std::to_string(validStepCount + 1) + " ('" + runState.completedSteps[validStepCount] +
                                "') is missing or outdated: resuming from there", false);

            // Sans consigne de départ, on reprend au premier step à refaire. Sinon on cherche le step demandé.
            if (runState.startFromStep.empty()) return validStepCount;
            for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
                if (stepName(*steps[stepIndex]) != runState.startFromStep) continue;
                if (stepIndex > validStepCount)
                    throw std::invalid_argument("cannot start at '" + runState.startFromStep + "': step " + std::to_string(validStepCount + 1) +
                                                " ('" + stepName(*steps[validStepCount]) + "') must be done first");
                return stepIndex;
            }
            throw std::invalid_argument("run.startFromStep: unknown step '" + runState.startFromStep + "'");
        }

        std::string currentTimestamp() {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm utcTime{};
#ifdef _WIN32
            gmtime_s(&utcTime, &now);
#else
            gmtime_r(&now, &utcTime);
#endif
            std::ostringstream timestamp;
            timestamp << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
            return timestamp.str();
        }

        Json createNotReachedStepReport(size_t stepIndex, const StepNs::StepClass& step) {
            const ConfigNs::StepConfig config = step.getConfigJson().get<ConfigNs::StepConfig>();
            return {
                {"index", stepIndex},
                {"name", config.name},
                {"status", "notReached"},
                {"summary", "Étape non exécutée."},
                {"reason", nullptr},
                {"configuration", step.getConfigJson()},
                {"parameters", step.getConfigJson().value("parameters", Json::array())},
                {"inputs", Json::array()},
                {"outputs", Json::array()},
                {"result", nullptr}
            };
        }

        Json createRunReport(size_t startIndex, const std::vector<std::unique_ptr<StepNs::StepClass>>& steps,
                             const CheckpointNs::CheckpointClass& checkpoint, const Json& metadata,
                             const fs::path& reportPath) {
            Json reportJson = {{"metadata", metadata}, {"steps", Json::array()}};
            if (!reportJson["metadata"].contains("startedAt"))
                reportJson["metadata"]["startedAt"] = currentTimestamp();
            reportJson["metadata"]["status"] = "running";

            StepNs::DataStoreClass reportDataStore;
            for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
                const StepNs::StepClass& step = *steps[stepIndex];
                if (reportPath.empty() || stepIndex >= startIndex) {
                    reportJson["steps"].push_back(createNotReachedStepReport(stepIndex, step));
                    continue;
                }

                const Json inputs = createStepInputs(step, reportDataStore);
                for (const ConfigNs::DataSlot& output : step.getConfigJson().get<ConfigNs::StepConfig>().outputs)
                    checkpoint.load(stepIndex, step, output.key, output.dataType, reportDataStore);
                Json stepReport = createStepReport(stepIndex, step, reportDataStore, inputs);
                stepReport["status"] = "skipped";
                stepReport["summary"] = "Résultats réutilisés depuis un checkpoint validé ; calcul non relancé.";
                stepReport["result"] = nullptr;
                stepReport["reason"] = nullptr;
                reportJson["steps"].push_back(std::move(stepReport));
            }
            return reportJson;
        }

        void saveReportStatus(Json& reportJson, const std::string& status,
                              const fs::path& reportPath, const PipelineClass::Logger& log) {
            if (reportPath.empty()) return;
            reportJson["metadata"]["status"] = status;
            if (status != "running") {
                reportJson["metadata"]["finishedAt"] = currentTimestamp();
                if (status == "failed" || status == "paused") {
                    for (Json& step : reportJson["steps"]) {
                        if (step.value("status", std::string()) == "notReached")
                            step["reason"] = "Étape non atteinte : l'exécution s'est arrêtée avant cette étape.";
                    }
                }
            }
            try {
                writeReport(reportPath, reportJson);
            } catch (const std::exception& exception) {
                report(log, std::string("warning: cannot update report: ") + exception.what(), true);
            }
        }

    }

    void PipelineClass::configure(const Json& scriptJson) {
        // Le script doit avoir exactement autant de steps que le pipeline : configure ne crée ni ne supprime de step.
        if (!scriptJson.is_object() || !scriptJson.contains("pipeline") || !scriptJson.at("pipeline").is_object() ||
            !scriptJson.at("pipeline").contains("steps") || !scriptJson.at("pipeline").at("steps").is_array())
            throw std::invalid_argument("expected { \"pipeline\": { \"steps\": [...] } }");
        const Json& stepsJson = scriptJson.at("pipeline").at("steps");
        if (stepsJson.size() != steps.size())
            throw std::invalid_argument("pipeline JSON has " + std::to_string(stepsJson.size()) +
                                        " steps, expected " + std::to_string(steps.size()));
        for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
            try {
                steps[stepIndex]->importConfig(stepsJson.at(stepIndex));
            } catch (const std::exception& exception) {
                throw std::invalid_argument("pipeline.steps[" + std::to_string(stepIndex) + "]: " + exception.what());
            }
        }
        if (scriptJson.contains("run")) runState = RunState::fromJson(scriptJson.at("run"));
    }

    // Chaque step est converti de son JSON en StepConfig ; un step sans config (JSON non objet) donne une config vide.
    Json PipelineClass::exportConfig() const {
        ConfigNs::PipelineConfig pipelineConfig;
        for (const auto& step : steps)
            pipelineConfig.steps.push_back(step->getConfigJson().is_object() ? step->getConfigJson().get<ConfigNs::StepConfig>() : ConfigNs::StepConfig{});
        return {{"pipeline", pipelineConfig}};
    }

    // Le script complet = la configuration + l'état du run : c'est ce fichier qui permet de reprendre un run interrompu.
    Json PipelineClass::exportScript() const {
        Json script = exportConfig();
        script["run"] = runState;
        return script;
    }

    void PipelineClass::saveScript(const Logger& log) const {
        if (scriptPath.empty()) return;
        try {
            if (scriptPath.has_parent_path()) fs::create_directories(scriptPath.parent_path());
            // Fichier temporaire puis renommage : le script n'est jamais écrit à moitié.
            fs::path temporaryPath = scriptPath;
            temporaryPath += ".tmp";
            {
                std::ofstream output(temporaryPath, std::ios::binary);
                output << exportScript().dump(2) << '\n';
                if (!output) throw std::runtime_error("write failed");
            }
#ifdef _WIN32
            if (!MoveFileExW(temporaryPath.c_str(), scriptPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "cannot replace " + scriptPath.string());
#else
            fs::rename(temporaryPath, scriptPath);
#endif
        } catch (const std::exception& exception) {
            report(log, "warning: cannot write " + scriptPath.string() + ": " + exception.what(), true);
        }
    }

    // Quatre phases : 1. choisir le step de départ ; 2. remettre l'état à zéro ; 3. restaurer les sorties des steps sautés ;
    // 4. exécuter les steps restants. Le script est réécrit à chaque changement d'état, pour qu'un crash soit rattrapable.
    bool PipelineClass::run(const Logger& log) {
        const size_t stepCount = steps.size();
        const CheckpointNs::CheckpointClass checkpoint(runState.checkpointDirectory);

        // 1. Où démarrer ?
        size_t startIndex = 0;
        if (runState.skipCompletedSteps) startIndex = resolveStart(steps, runState, checkpoint, log);
        else if (!runState.startFromStep.empty()) report(log, "run.startFromStep ignored: skipCompletedSteps=false restarts from the first step", false);
        if (startIndex > 0 && startIndex < stepCount) report(log, "resuming at step " + std::to_string(startIndex + 1) + "/" + std::to_string(stepCount), false);

        // 2. État propre : les checkpoints des steps à refaire sont périmés, les consignes sont consommées.
        checkpoint.removeFrom(startIndex);
        runState.completedSteps.resize(startIndex);
        runState.skipCompletedSteps = true;
        runState.startFromStep.clear();
        dataStore = StepNs::DataStoreClass();

        // 3. Restauration des sorties des steps sautés : pour chaque clé, la version du dernier step qui l'a produite.
        std::map<std::string, std::pair<size_t, std::string>> latestOutputByKey;   // clé -> (index du step producteur, type)
        for (size_t stepIndex = 0; stepIndex < startIndex; ++stepIndex)
            for (const ConfigNs::DataSlot& outputSlot : steps[stepIndex]->getConfigJson().get<ConfigNs::StepConfig>().outputs)
                latestOutputByKey[outputSlot.key] = {stepIndex, outputSlot.dataType};
        try {
            for (const auto& [key, producer] : latestOutputByKey) checkpoint.load(producer.first, *steps[producer.first], key, producer.second, dataStore);
        } catch (const std::exception& exception) {
            report(log, std::string("cannot restore checkpoint: ") + exception.what(), true);
            runState.status = RunStatus::Failed;
            saveScript(log);
            return false;
        }

        Json reportJson;
        try {
            reportJson = createRunReport(startIndex, steps, checkpoint, reportMetadata, reportPath);
        } catch (const std::exception& exception) {
            report(log, std::string("cannot prepare execution report: ") + exception.what(), true);
            runState.status = RunStatus::Failed;
            saveScript(log);
            return false;
        }

        // 4. Exécution des steps restants.
        runState.status = RunStatus::Running;
        saveScript(log);
        for (size_t stepIndex = startIndex; stepIndex < stepCount; ++stepIndex) {
            const auto& step = steps[stepIndex];
            const std::string messagePrefix = "Step " + std::to_string(stepIndex + 1) + "/" + std::to_string(stepCount);
            Json& stepReport = reportJson["steps"].at(stepIndex);

            if (pauseRequested->exchange(false)) {   // pause demandée : arrêt propre entre deux steps
                runState.status = RunStatus::Paused;
                saveReportStatus(reportJson, "paused", reportPath, log);
                saveScript(log);
                report(log, messagePrefix + " paused", false);
                return false;
            }

            step->setDataStore(dataStore);
            Json stepInputs;
            if (!reportPath.empty()) {
                try {
                    stepInputs = createStepInputs(*step, dataStore);
                } catch (const std::exception& exception) {
                    report(log, messagePrefix + " failed to capture report inputs: " + exception.what(), true);
                    stepReport["status"] = "failed";
                    stepReport["summary"] = "Impossible de documenter les entrées de l'étape.";
                    stepReport["reason"] = exception.what();
                    saveReportStatus(reportJson, "failed", reportPath, log);
                    runState.status = RunStatus::Failed;
                    saveScript(log);
                    return false;
                }
                stepReport["status"] = "running";
                stepReport["summary"] = "Étape en cours.";
                stepReport["inputs"] = stepInputs;
                try {
                    writeReport(reportPath, reportJson);
                } catch (const std::exception& exception) {
                    report(log, messagePrefix + " failed to update execution report: " + exception.what(), true);
                    runState.status = RunStatus::Failed;
                    saveScript(log);
                    return false;
                }
            }
            bool succeeded = false;
            std::string errorMessage;
            try {
                succeeded = step->execute();
            } catch (const std::exception& exception) {   // une exception d'un step = un step en échec, pas un crash
                errorMessage = exception.what();
            }
            if (!succeeded) {
                const Json result = step->getResult();
                std::string failureReason = errorMessage;
                if (failureReason.empty()) {
                    if (result.is_object() && result.contains("error")) {
                        const Json& error = result.at("error");
                        failureReason = error.is_string() ? error.get<std::string>() : error.dump();
                    } else {
                        failureReason = result.dump();
                    }
                }
                std::string failureMessage = messagePrefix;
                failureMessage += " failed: ";
                failureMessage += failureReason;
                report(log, failureMessage, true);
                if (!reportPath.empty()) {
                    stepReport["status"] = "failed";
                    stepReport["summary"] = "L'étape a échoué.";
                    stepReport["reason"] = failureReason;
                    stepReport["result"] = result;
                    saveReportStatus(reportJson, "failed", reportPath, log);
                }
                runState.status = RunStatus::Failed;
                saveScript(log);
                return false;   // reprise possible : completedSteps et checkpoints sont à jour
            }

            // Step réussi : on sauvegarde ses sorties PUIS on le déclare fait, jamais l'inverse.
            std::string failureReason;
            if (checkpoint.enabled() && !checkpoint.save(stepIndex, *step, dataStore, failureReason)) {
                std::string warningMessage = messagePrefix;
                warningMessage += " warning: checkpoint incomplete (";
                warningMessage += failureReason;
                warningMessage += "), this step will be redone on resume";
                report(log, warningMessage, false);
            }
            if (!reportPath.empty()) {
                try {
                    stepReport = createStepReport(stepIndex, *step, dataStore, stepInputs);
                    if (stepName(*step) == "export" && stepReport["result"].is_object() &&
                        stepReport["result"].contains("image"))
                        reportJson["output"] = {
                            {"path", stepReport["result"]["image"].value("path", std::string())},
                            {"dimensions", stepReport["result"].value("canvasSize", Json::array())}
                        };
                    writeReport(reportPath, reportJson);
                } catch (const std::exception& exception) {
                    report(log, messagePrefix + " failed to update execution report: " + exception.what(), true);
                    runState.status = RunStatus::Failed;
                    saveScript(log);
                    return false;
                }
            }
            runState.completedSteps.push_back(stepName(*step));
            saveScript(log);
            report(log, messagePrefix + " OK " + step->getResult().dump(), false);
        }
        if (startIndex == stepCount && stepCount > 0) report(log, "nothing to do: all steps are already done", false);
        runState.status = RunStatus::Completed;
        if (!reportPath.empty()) {
            try {
                reportJson["metadata"]["status"] = "completed";
                reportJson["metadata"]["finishedAt"] = currentTimestamp();
                writeReport(reportPath, reportJson);
            } catch (const std::exception& exception) {
                report(log, std::string("failed to finalize execution report: ") + exception.what(), true);
                runState.status = RunStatus::Failed;
                saveScript(log);
                return false;
            }
        }
        saveScript(log);
        return true;
    }

}
