// src/backend/core/pipeline/pipeline.cpp
#include "core/pipeline/pipeline.h"

#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    namespace fs = std::filesystem; // NOLINT(readability-identifier-naming)

    namespace {

        // Nom du step lu dans sa config JSON ; chaîne vide si le step n'a pas de config.
        std::string stepName(const StepNs::StepClass& step) {
            return step.getConfigJson().is_object() ? step.getConfigJson().value("name", "") : "";
        }

        struct CheckpointProducer {
            std::size_t stepIndex;
            ConfigNs::DataSlot output;
        };

        struct OutputRestoreContext {
            const std::vector<std::unique_ptr<StepNs::StepClass>>& steps;
            const CheckpointNs::CheckpointClass& checkpoint;
            StepNs::DataStoreClass& dataStore;
            const PipelineClass::Logger& logger;
        };

        void report(const PipelineClass::Logger& log, const std::string& message, bool isError);

        std::optional<std::size_t> restoreOutputValues(
            const std::map<std::string, CheckpointProducer>& outputs,
            const OutputRestoreContext& context) {
            for (const auto& entry : outputs) {
                const CheckpointProducer& producer = entry.second;
                try {
                    context.checkpoint.load(producer.stepIndex,
                                            *context.steps[producer.stepIndex],
                                            producer.output, context.dataStore);
                } catch (const std::exception& exception) {
                    std::string message = "checkpoint of step ";
                    message += std::to_string(producer.stepIndex + 1);
                    message += " is unreadable: ";
                    message += exception.what();
                    message += "; recomputing from that step";
                    report(context.logger, message, false);
                    return producer.stepIndex;
                }
            }
            return std::nullopt;
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
            const fs::path temporaryPath = scriptPath.string() + ".tmp";
            {
                std::ofstream output(temporaryPath, std::ios::binary);
                output << exportScript().dump(2) << '\n';
                if (!output) throw std::runtime_error("write failed");
            }
            fs::rename(temporaryPath, scriptPath);
        } catch (const std::exception& exception) {
            report(log, "warning: cannot write " + scriptPath.string() + ": " + exception.what(), true);
        }
    }

    void PipelineClass::prepareRun(size_t startIndex,
                                   const CheckpointNs::CheckpointClass& checkpoint) {
        checkpoint.removeFrom(startIndex);
        runState.completedSteps.resize(startIndex);
        for (size_t stepIndex = startIndex; stepIndex < steps.size(); ++stepIndex)
            runState.stepResults.erase(stepName(*steps[stepIndex]));
        runState.failedStep.clear();
        runState.error.clear();
        runState.skipCompletedSteps = true;
        runState.startFromStep.clear();
        dataStore = StepNs::DataStoreClass();
    }

    std::string PipelineClass::stepMessagePrefix(size_t stepIndex) const {
        std::string prefix = "Step ";
        prefix += std::to_string(stepIndex + 1);
        prefix.push_back('/');
        prefix += std::to_string(steps.size());
        return prefix;
    }

    void PipelineClass::restoreOutputCheckpoints(
        size_t& startIndex, const CheckpointNs::CheckpointClass& checkpoint,
        const Logger& log) {
        std::map<std::string, CheckpointProducer> latestOutputByKey;
        for (size_t stepIndex = 0; stepIndex < startIndex; ++stepIndex) {
            const ConfigNs::StepConfig stepConfig =
                steps[stepIndex]->getConfigJson().get<ConfigNs::StepConfig>();
            for (const ConfigNs::DataSlot& output : stepConfig.outputs)
                latestOutputByKey[output.key] = {stepIndex, output};
        }
        while (true) {
            dataStore = StepNs::DataStoreClass();
            const OutputRestoreContext context{steps, checkpoint, dataStore, log};
            const std::optional<size_t> brokenStep =
                restoreOutputValues(latestOutputByKey, context);
            if (!brokenStep)
                return;
            startIndex = *brokenStep;
            checkpoint.removeFrom(startIndex);
            runState.completedSteps.resize(startIndex);
            for (size_t stepIndex = startIndex; stepIndex < steps.size(); ++stepIndex)
                runState.stepResults.erase(stepName(*steps[stepIndex]));
        }
    }

    bool PipelineClass::recordStepFailure(const StepNs::StepClass& step,
                                          const StepFailureDetails& details,
                                          const Logger& log) {
        const std::string name = stepName(step);
        const Json result = step.getResult();
        const std::string failure = details.errorMessage.empty()
            ? result.dump() : details.errorMessage;
        std::string message = details.messagePrefix;
        message += " failed: ";
        message += failure;
        report(log, message, true);
        runState.status = RunStatus::Failed;
        runState.failedStep = name;
        const Json stepError = result.is_object() ? result.value("error", Json()) : Json();
        runState.error = !details.errorMessage.empty()
            ? details.errorMessage : (stepError.is_string() ? stepError.get<std::string>() : failure);
        runState.stepResults[name] = result;
        saveScript(log);
        return false;
    }

    void PipelineClass::recordStepSuccess(
        size_t stepIndex, const CheckpointNs::CheckpointClass& checkpoint,
        const Logger& log) {
        const auto& step = steps[stepIndex];
        const std::string prefix = stepMessagePrefix(stepIndex);
        std::string failureReason;
        if (checkpoint.enabled() &&
            !checkpoint.save(stepIndex, *step, dataStore, failureReason)) {
            std::string warning = prefix;
            warning += " warning: checkpoint incomplete (";
            warning += failureReason;
            warning += "), this step will be redone on resume";
            report(log, warning, false);
        }
        const std::string name = stepName(*step);
        runState.completedSteps.push_back(name);
        runState.stepResults[name] = step->getResult();
        saveScript(log);
        report(log, prefix + " OK " + step->getResult().dump(), false);
    }

    bool PipelineClass::executeStep(size_t stepIndex,
                                    const CheckpointNs::CheckpointClass& checkpoint,
                                    const Logger& log) {
        const auto& step = steps[stepIndex];
        const std::string messagePrefix = stepMessagePrefix(stepIndex);
        if (pauseRequested->exchange(false)) {
            runState.status = RunStatus::Paused;
            saveScript(log);
            std::string pausedMessage = messagePrefix;
            pausedMessage += " paused";
            report(log, pausedMessage, false);
            return false;
        }
        step->setDataStore(dataStore);
        bool succeeded = false;
        std::string errorMessage;
        try {
            succeeded = step->execute();
        } catch (const std::exception& exception) {
            errorMessage = exception.what();
        }
        if (!succeeded)
            return recordStepFailure(*step, {messagePrefix, errorMessage}, log);
        recordStepSuccess(stepIndex, checkpoint, log);
        return true;
    }

    bool PipelineClass::executeRemainingSteps(
        size_t startIndex, const CheckpointNs::CheckpointClass& checkpoint,
        const Logger& log) {
        for (size_t stepIndex = startIndex; stepIndex < steps.size(); ++stepIndex)
            if (!executeStep(stepIndex, checkpoint, log))
                return false;
        return true;
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

        prepareRun(startIndex, checkpoint);
        restoreOutputCheckpoints(startIndex, checkpoint, log);
        runState.status = RunStatus::Running;
        saveScript(log);
        if (!executeRemainingSteps(startIndex, checkpoint, log))
            return false;
        if (startIndex == stepCount && stepCount > 0) report(log, "nothing to do: all steps are already done", false);
        runState.status = RunStatus::Completed;
        saveScript(log);
        return true;
    }

}
