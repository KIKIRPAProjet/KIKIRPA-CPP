// src/backend/core/pipeline/pipeline.cpp
#include "core/pipeline/pipeline.h"

#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/pipeline/pipeline_report.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <system_error>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PipelineNs {

    namespace FilesystemNs = std::filesystem;

    namespace {

        struct PipelineRunContext {
            const std::vector<std::unique_ptr<StepNs::StepClass>>& steps;
            StepNs::DataStoreClass& dataStore;
            RunState& runState;
            const FilesystemNs::path& reportPath;
            const Json& reportMetadata;
            const std::shared_ptr<std::atomic<bool>>& pauseRequested;
            const CheckpointNs::CheckpointClass& checkpoint;
            const PipelineClass::Logger& log;
            const std::function<void()>& saveScript;
            Json reportJson;
            size_t startIndex = 0;
            size_t stepIndex = 0;
            std::string messagePrefix;
            Json stepInputs;
        };

        // Nom du step lu dans sa config JSON ; chaîne vide si le step n'a pas de config.
        std::string getStepName(const StepNs::StepClass& step) {
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
                runState.completedSteps[validStepCount] == getStepName(*steps[validStepCount]) &&
                checkpoint.valid(validStepCount, *steps[validStepCount]))
                ++validStepCount;
            if (checkpoint.enabled() && validStepCount < runState.completedSteps.size())
                report(log, "checkpoint of step " + std::to_string(validStepCount + 1) + " ('" + runState.completedSteps[validStepCount] +
                                "') is missing or outdated: resuming from there", false);

            // Sans consigne de départ, on reprend au premier step à refaire. Sinon on cherche le step demandé.
            if (runState.startFromStep.empty()) return validStepCount;
            for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
                if (getStepName(*steps[stepIndex]) != runState.startFromStep) continue;
                if (stepIndex > validStepCount)
                    throw std::invalid_argument("cannot start at '" + runState.startFromStep + "': step " + std::to_string(validStepCount + 1) +
                                                " ('" + getStepName(*steps[validStepCount]) + "') must be done first");
                return stepIndex;
            }
            throw std::invalid_argument("run.startFromStep: unknown step '" + runState.startFromStep + "'");
        }

        std::string getCurrentTimestamp() {
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

        Json createInitialReport(const Json& metadata) {
            Json reportJson = {{"metadata", metadata}, {"steps", Json::array()}};
            if (!reportJson["metadata"].contains("startedAt"))
                reportJson["metadata"]["startedAt"] = getCurrentTimestamp();
            reportJson["metadata"]["status"] = "running";
            return reportJson;
        }

        Json createSkippedStepReport(size_t stepIndex, const StepNs::StepClass& step,
                                     const CheckpointNs::CheckpointClass& checkpoint,
                                     StepNs::DataStoreClass& reportDataStore) {
            const Json inputs = createStepInputs(step, reportDataStore);
            for (const ConfigNs::DataSlot& output : step.getConfigJson().get<ConfigNs::StepConfig>().outputs)
                checkpoint.load(stepIndex, step, output.key, output.dataType, reportDataStore);
            Json stepReport = createStepReport(stepIndex, step, reportDataStore, inputs);
            stepReport["status"] = "skipped";
            stepReport["summary"] = "Résultats réutilisés depuis un checkpoint validé ; calcul non relancé.";
            stepReport["result"] = nullptr;
            stepReport["reason"] = nullptr;
            return stepReport;
        }

        struct RunReportContext {
            size_t startIndex;
            const std::vector<std::unique_ptr<StepNs::StepClass>>& steps;
            const CheckpointNs::CheckpointClass& checkpoint;
            const FilesystemNs::path& reportPath;
            StepNs::DataStoreClass& reportDataStore;
            Json& reportJson;
            const Json& metadata;
        };

        void appendRunStepReport(size_t stepIndex, RunReportContext& context) {
            const StepNs::StepClass& step = *context.steps[stepIndex];
            if (context.reportPath.empty() || stepIndex >= context.startIndex) {
                context.reportJson["steps"].push_back(createNotReachedStepReport(stepIndex, step));
                return;
            }
            context.reportJson["steps"].push_back(
                createSkippedStepReport(stepIndex, step, context.checkpoint, context.reportDataStore));
        }

        void createRunReport(RunReportContext& context) {
            context.reportJson = createInitialReport(context.metadata);
            for (size_t stepIndex = 0; stepIndex < context.steps.size(); ++stepIndex)
                appendRunStepReport(stepIndex, context);
        }

        void markUnreachedSteps(Json& reportJson) {
            for (Json& step : reportJson["steps"]) {
                if (step.value("status", std::string()) == "notReached")
                    step["reason"] = "Étape non atteinte : l'exécution s'est arrêtée avant cette étape.";
            }
        }

        void saveReportStatus(Json& reportJson, const std::string& status,
                              const FilesystemNs::path& reportPath, const PipelineClass::Logger& log) {
            if (reportPath.empty()) return;
            reportJson["metadata"]["status"] = status;
            if (status != "running") reportJson["metadata"]["finishedAt"] = getCurrentTimestamp();
            if (status == "failed" || status == "paused") markUnreachedSteps(reportJson);
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

    namespace {

        void saveScriptFile(const FilesystemNs::path& scriptPath, const Json& script, const PipelineClass::Logger& log) {
            if (scriptPath.empty()) return;
            try {
                if (scriptPath.has_parent_path()) FilesystemNs::create_directories(scriptPath.parent_path());
                FilesystemNs::path temporaryPath = scriptPath;
                temporaryPath += ".tmp";
                {
                    std::ofstream output(temporaryPath, std::ios::binary);
                    output << script.dump(2) << '\n';
                    if (!output) throw std::runtime_error("write failed");
                }
#ifdef _WIN32
                if (!MoveFileExW(temporaryPath.c_str(), scriptPath.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                            "cannot replace " + scriptPath.string());
#else
                FilesystemNs::rename(temporaryPath, scriptPath);
#endif
            } catch (const std::exception& exception) {
                report(log, "warning: cannot write " + scriptPath.string() + ": " + exception.what(), true);
            }
        }

        bool restoreCompletedOutputs(PipelineRunContext& context) {
            std::map<std::string, std::pair<size_t, std::string>> latestOutputByKey;
            for (size_t stepIndex = 0; stepIndex < context.startIndex; ++stepIndex)
                for (const ConfigNs::DataSlot& output : context.steps[stepIndex]->getConfigJson()
                         .get<ConfigNs::StepConfig>().outputs)
                    latestOutputByKey[output.key] = {stepIndex, output.dataType};
            try {
                for (const auto& [key, producer] : latestOutputByKey)
                    context.checkpoint.load(producer.first, *context.steps[producer.first], key,
                                            producer.second, context.dataStore);
            } catch (const std::exception& exception) {
                report(context.log, std::string("cannot restore checkpoint: ") + exception.what(), true);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            return true;
        }

        bool prepareRun(PipelineRunContext& context) {
            context.startIndex = context.runState.skipCompletedSteps
                ? resolveStart(context.steps, context.runState, context.checkpoint, context.log) : 0;
            if (!context.runState.skipCompletedSteps && !context.runState.startFromStep.empty())
                report(context.log, "run.startFromStep ignored: skipCompletedSteps=false restarts from the first step", false);
            if (context.startIndex > 0 && context.startIndex < context.steps.size())
                report(context.log, "resuming at step " + std::to_string(context.startIndex + 1) + "/" +
                                    std::to_string(context.steps.size()), false);
            context.checkpoint.removeFrom(context.startIndex);
            context.runState.completedSteps.resize(context.startIndex);
            context.runState.skipCompletedSteps = true;
            context.runState.startFromStep.clear();
            context.dataStore = StepNs::DataStoreClass();
            if (!restoreCompletedOutputs(context)) return false;
            try {
                StepNs::DataStoreClass reportDataStore;
                RunReportContext reportContext{context.startIndex, context.steps, context.checkpoint,
                    context.reportPath, reportDataStore, context.reportJson, context.reportMetadata};
                createRunReport(reportContext);
            } catch (const std::exception& exception) {
                report(context.log, std::string("cannot prepare execution report: ") + exception.what(), true);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            return true;
        }

        bool handlePause(PipelineRunContext& context) {
            if (!context.pauseRequested->exchange(false)) return false;
            context.runState.status = RunStatus::Paused;
            saveReportStatus(context.reportJson, "paused", context.reportPath, context.log);
            context.saveScript();
            report(context.log, context.messagePrefix + " paused", false);
            return true;
        }

        bool writeRunningStepReport(PipelineRunContext& context) {
            try {
                writeReport(context.reportPath, context.reportJson);
            } catch (const std::exception& exception) {
                report(context.log, context.messagePrefix + " failed to update execution report: " +
                                    exception.what(), true);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            return true;
        }

        bool captureStepInputs(PipelineRunContext& context) {
            if (context.reportPath.empty()) return true;
            try {
                context.stepInputs = createStepInputs(*context.steps[context.stepIndex], context.dataStore);
            } catch (const std::exception& exception) {
                Json& stepReport = context.reportJson["steps"].at(context.stepIndex);
                report(context.log, context.messagePrefix + " failed to capture report inputs: " +
                                    exception.what(), true);
                stepReport["status"] = "failed";
                stepReport["summary"] = "Impossible de documenter les entrées de l'étape.";
                stepReport["reason"] = exception.what();
                saveReportStatus(context.reportJson, "failed", context.reportPath, context.log);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            Json& stepReport = context.reportJson["steps"].at(context.stepIndex);
            stepReport["status"] = "running";
            stepReport["summary"] = "Étape en cours.";
            stepReport["inputs"] = context.stepInputs;
            return writeRunningStepReport(context);
        }

        bool executeConfiguredStep(PipelineRunContext& context, std::string& errorMessage) {
            try {
                return context.steps[context.stepIndex]->execute();
            } catch (const std::exception& exception) {
                errorMessage = exception.what();
                return false;
            }
        }

        bool recordFailedStep(const std::string& failureReason, const Json& result,
                              PipelineRunContext& context) {
            std::string failureMessage = context.messagePrefix;
            failureMessage += " failed: ";
            failureMessage += failureReason;
            report(context.log, failureMessage, true);
            if (!context.reportPath.empty()) {
                Json& stepReport = context.reportJson["steps"].at(context.stepIndex);
                stepReport["status"] = "failed";
                stepReport["summary"] = "L'étape a échoué.";
                stepReport["reason"] = failureReason;
                stepReport["result"] = result;
                saveReportStatus(context.reportJson, "failed", context.reportPath, context.log);
            }
            context.runState.status = RunStatus::Failed;
            context.saveScript();
            return false;
        }

        void saveSuccessfulCheckpoint(PipelineRunContext& context) {
            std::string failureReason;
            if (!context.checkpoint.enabled() ||
                context.checkpoint.save(context.stepIndex, *context.steps[context.stepIndex],
                                        context.dataStore, failureReason)) return;
            std::string warning = context.messagePrefix + " warning: checkpoint incomplete (";
            warning += failureReason;
            warning += "), this step will be redone on resume";
            report(context.log, warning, false);
        }

        bool writeSuccessfulStepReport(PipelineRunContext& context) {
            if (context.reportPath.empty()) return true;
            try {
                Json& stepReport = context.reportJson["steps"].at(context.stepIndex);
                stepReport = createStepReport(context.stepIndex, *context.steps[context.stepIndex],
                                              context.dataStore, context.stepInputs);
                if (getStepName(*context.steps[context.stepIndex]) == "export" &&
                    stepReport["result"].is_object() && stepReport["result"].contains("image"))
                    context.reportJson["output"] = {
                        {"path", stepReport["result"]["image"].value("path", std::string())},
                        {"dimensions", stepReport["result"].value("canvasSize", Json::array())}
                    };
                writeReport(context.reportPath, context.reportJson);
            } catch (const std::exception& exception) {
                report(context.log, context.messagePrefix + " failed to update execution report: " +
                                    exception.what(), true);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            return true;
        }

        bool recordSuccessfulStep(PipelineRunContext& context) {
            if (!writeSuccessfulStepReport(context)) return false;
            context.runState.completedSteps.push_back(getStepName(*context.steps[context.stepIndex]));
            context.saveScript();
            report(context.log, context.messagePrefix + " OK " +
                                context.steps[context.stepIndex]->getResult().dump(), false);
            return true;
        }

        bool executeStep(PipelineRunContext& context) {
            if (handlePause(context)) return false;
            context.steps[context.stepIndex]->setDataStore(context.dataStore);
            if (!captureStepInputs(context)) return false;
            std::string errorMessage;
            if (!executeConfiguredStep(context, errorMessage)) {
                const Json result = context.steps[context.stepIndex]->getResult();
                if (errorMessage.empty())
                    errorMessage = result.is_object() && result.contains("error")
                        ? (result.at("error").is_string() ? result.at("error").get<std::string>()
                                                          : result.at("error").dump())
                        : result.dump();
                return recordFailedStep(errorMessage, result, context);
            }
            saveSuccessfulCheckpoint(context);
            return recordSuccessfulStep(context);
        }

        bool executeSteps(PipelineRunContext& context) {
            for (context.stepIndex = context.startIndex; context.stepIndex < context.steps.size();
                 ++context.stepIndex) {
                context.messagePrefix = "Step " + std::to_string(context.stepIndex + 1) + "/" +
                    std::to_string(context.steps.size());
                if (!executeStep(context)) return false;
            }
            return true;
        }

        bool finalizeRun(PipelineRunContext& context) {
            if (context.startIndex == context.steps.size() && !context.steps.empty())
                report(context.log, "nothing to do: all steps are already done", false);
            context.runState.status = RunStatus::Completed;
            try {
                if (!context.reportPath.empty()) {
                    context.reportJson["metadata"]["status"] = "completed";
                    context.reportJson["metadata"]["finishedAt"] = getCurrentTimestamp();
                    writeReport(context.reportPath, context.reportJson);
                }
            } catch (const std::exception& exception) {
                report(context.log, std::string("failed to finalize execution report: ") + exception.what(), true);
                context.runState.status = RunStatus::Failed;
                context.saveScript();
                return false;
            }
            context.saveScript();
            return true;
        }

        bool runPipeline(PipelineRunContext& context) {
            if (!prepareRun(context)) return false;
            context.runState.status = RunStatus::Running;
            context.saveScript();
            if (!executeSteps(context)) return false;
            return finalizeRun(context);
        }

    }

    bool PipelineClass::run(const Logger& log) {
        const CheckpointNs::CheckpointClass checkpoint(runState.checkpointDirectory);
        const auto saveScript = [this, &log]() {
            saveScriptFile(scriptPath, exportScript(), log);
        };
        PipelineRunContext context{steps, dataStore, runState, reportPath, reportMetadata,
                                   pauseRequested, checkpoint, log, saveScript};
        return runPipeline(context);
    }

}
