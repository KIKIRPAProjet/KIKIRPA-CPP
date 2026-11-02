// src/Backend/core/pipeline/pipeline.cpp
#include "core/pipeline/pipeline.h"

#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    namespace fs = std::filesystem;

    namespace {

        std::string stepName(const StepNs::StepClass& step) {
            return step.getConfigJson().is_object() ? step.getConfigJson().value("name", "") : "";
        }

        void report(const PipelineClass::Logger& log, const std::string& message, bool isError) {
            if (log) log(message);
            else if (isError) std::cerr << message << '\n';
        }

        // Index du step où démarrer :
        //  - valid = nombre de steps de tête déjà faits (run.completedSteps), de même nom, dont le checkpoint est complet
        //    et à jour (mêmes options). Dès qu'un step est invalide, lui et tous les suivants sont à refaire ;
        //  - run.from permet de revenir plus tôt (refaire un step), jamais d'aller au-delà de valid (données manquantes).
        size_t resolveStart(const std::vector<std::unique_ptr<StepNs::StepClass>>& steps, const RunState& runState,
                            const CheckpointNs::CheckpointClass& checkpoint, const PipelineClass::Logger& log) {
            size_t validStepCount = 0;
            while (validStepCount < runState.completedSteps.size() && validStepCount < steps.size() &&
                runState.completedSteps[validStepCount] == stepName(*steps[validStepCount]) && checkpoint.valid(validStepCount, *steps[validStepCount]))
                ++validStepCount;
            if (checkpoint.enabled() && validStepCount < runState.completedSteps.size())
                report(log, "checkpoint of step " + std::to_string(validStepCount + 1) + " ('" + runState.completedSteps[validStepCount] +
                                "') is missing or outdated: resuming from there", false);

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

    Json PipelineClass::exportConfig() const {
        ConfigNs::PipelineConfig pipelineConfig;
        for (const auto& step : steps)
            pipelineConfig.steps.push_back(step->getConfigJson().is_object() ? step->getConfigJson().get<ConfigNs::StepConfig>() : ConfigNs::StepConfig{});
        return {{"pipeline", pipelineConfig}};
    }

    Json PipelineClass::exportScript() const {
        Json script = exportConfig();
        script["run"] = runState;
        return script;
    }

    // Écriture atomique (fichier temporaire puis rename). Un échec n'arrête pas le pipeline, il est seulement signalé.
    void PipelineClass::saveScript(const Logger& log) const {
        if (scriptPath.empty()) return;
        try {
            if (scriptPath.has_parent_path()) fs::create_directories(scriptPath.parent_path());
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

    bool PipelineClass::run(const Logger& log) {
        const size_t stepCount = steps.size();
        const CheckpointNs::CheckpointClass checkpoint(runState.checkpointDirectory);

        // 1. Où démarrer ?
        size_t startIndex = 0;
        if (runState.skipCompletedSteps) startIndex = resolveStart(steps, runState, checkpoint, log);
        else if (!runState.startFromStep.empty()) report(log, "run.startFromStep ignored: skipCompletedSteps=false restarts from the first step", false);
        if (startIndex > 0 && startIndex < stepCount) report(log, "resuming at step " + std::to_string(startIndex + 1) + "/" + std::to_string(stepCount), false);

        // 2. État propre : les checkpoints des steps à refaire sont périmés, les consignes sont consommées
        checkpoint.removeFrom(startIndex);
        runState.completedSteps.resize(startIndex);
        runState.skipCompletedSteps = true;
        runState.startFromStep.clear();
        dataStore = StepNs::DataStoreClass();

        // 3. Restauration des sorties des steps sautés (dernière version de chaque clé)
        std::map<std::string, std::pair<size_t, std::string>> latestOutputByKey;   // clé -> (step producteur, type)
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

        // 4. Exécution
        runState.status = RunStatus::Running;
        saveScript(log);
        for (size_t stepIndex = startIndex; stepIndex < stepCount; ++stepIndex) {
            const auto& step = steps[stepIndex];
            const std::string messagePrefix = "Step " + std::to_string(stepIndex + 1) + "/" + std::to_string(stepCount);

            if (pauseRequested->exchange(false)) {   // pause demandée : arrêt propre entre deux steps
                runState.status = RunStatus::Paused;
                saveScript(log);
                report(log, messagePrefix + " paused", false);
                return false;
            }

            step->setDataStore(dataStore);
            bool succeeded = false;
            std::string errorMessage;
            try {
                succeeded = step->execute();
            } catch (const std::exception& exception) {   // une exception d'un step = un step en échec, pas un crash
                errorMessage = exception.what();
            }
            if (!succeeded) {
                report(log, messagePrefix + " failed: " + (errorMessage.empty() ? step->getResult().dump() : errorMessage), true);
                runState.status = RunStatus::Failed;
                saveScript(log);
                return false;   // reprise possible : completedSteps et checkpoints sont à jour
            }

            // Step réussi : on sauvegarde ses sorties PUIS on le déclare fait (jamais l'inverse)
            std::string failureReason;
            if (checkpoint.enabled() && !checkpoint.save(stepIndex, *step, dataStore, failureReason))
                report(log, messagePrefix + " warning: checkpoint incomplete (" + failureReason + "), this step will be redone on resume", false);
            runState.completedSteps.push_back(stepName(*step));
            saveScript(log);
            report(log, messagePrefix + " OK " + step->getResult().dump(), false);
        }
        if (startIndex == stepCount && stepCount > 0) report(log, "nothing to do: all steps are already done", false);
        runState.status = RunStatus::Completed;
        saveScript(log);
        return true;
    }

}
