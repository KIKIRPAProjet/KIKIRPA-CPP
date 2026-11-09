// include/backend/core/pipeline/pipeline.h
#pragma once

#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CheckpointNs {
    class CheckpointClass;
}

namespace PipelineNs {

    // The JSON type is forward-declared here and provided by the backend JSON headers.
    // NOLINTBEGIN(misc-include-cleaner)
    using Json = nlohmann::json;
    // NOLINTEND(misc-include-cleaner)

    // Suite ordonnée de steps qui partagent un DataStoreClass, avec l'état de leur exécution (RunState).
    // Se construit avec buildPipeline (pipeline_builder.h) ou step par step avec addStep.
    class PipelineClass {

        public:
            // Reçoit chaque message de progression.
            using Logger = std::function<void(const std::string&)>;

            PipelineClass() = default;

            // Applique { "pipeline": { "steps": [...] }, "run": {...} } aux steps déjà ajoutés : steps[i] du JSON
            // configure le i-ème step ; "run" est facultatif. Lève std::invalid_argument si le JSON est invalide
            // ou si le nombre de steps diffère.
            void configure(const Json& scriptJson);
            // { "pipeline": ... } : la configuration seule.
            Json exportConfig() const;
            // { "pipeline": ..., "run": ... } : le script complet, qui permet de reprendre un run.
            Json exportScript() const;
            void addStep(std::unique_ptr<StepNs::StepClass> step) { steps.push_back(std::move(step)); }

            // Exécute les steps dans l'ordre. Renvoie false si un step échoue (retour false ou exception) ou si une
            // pause est demandée. Si runState.skipCompletedSteps est vrai et qu'un dossier de checkpoints est défini,
            // saute les steps déjà faits dont le checkpoint est valide et restaure leurs sorties ; startFromStep permet
            // de redémarrer plus tôt. Lève std::invalid_argument si startFromStep est inconnu ou situé après le premier
            // step à refaire. Les messages vont à `log` ; sans `log`, seuls les échecs vont sur std::cerr.
            bool run(const Logger& log = {});

            // Demande une pause, prise en compte entre deux steps (le step en cours va jusqu'au bout). Thread-safe.
            void requestPause() { pauseRequested->store(true); }

            // État et consignes du run (voir RunState).
            void setRunState(const RunState& newRunState) { runState = newRunState; }
            const RunState& getRunState() const { return runState; }
            // Fichier où le script complet (exportScript) est réécrit après chaque step ; vide : pas d'écriture.
            void setScriptPath(std::filesystem::path newScriptPath) { scriptPath = std::move(newScriptPath); }

            // Sorties des steps déjà exécutés.
            const StepNs::DataStoreClass& getDataStore() const { return dataStore; }
            // Steps dans l'ordre d'exécution.
            const std::vector<std::unique_ptr<StepNs::StepClass>>& getSteps() const { return steps; }

        private:
            // Réécrit le script complet dans scriptPath (écriture atomique) ; un échec est signalé sans arrêter le pipeline.
            void saveScript(const Logger& log) const;
            void prepareRun(size_t startIndex, const CheckpointNs::CheckpointClass& checkpoint);
            std::string stepMessagePrefix(size_t stepIndex) const;
            void restoreOutputCheckpoints(size_t& startIndex,
                                          const CheckpointNs::CheckpointClass& checkpoint,
                                          const Logger& log);
            bool executeRemainingSteps(size_t startIndex,
                                       const CheckpointNs::CheckpointClass& checkpoint,
                                       const Logger& log);
            bool executeStep(size_t stepIndex,
                             const CheckpointNs::CheckpointClass& checkpoint,
                             const Logger& log);
            struct StepFailureDetails {
                std::string messagePrefix;
                std::string errorMessage;
            };
            bool recordStepFailure(const StepNs::StepClass& step,
                                   const StepFailureDetails& details, const Logger& log);
            void recordStepSuccess(size_t stepIndex,
                                   const CheckpointNs::CheckpointClass& checkpoint,
                                   const Logger& log);

            std::vector<std::unique_ptr<StepNs::StepClass>> steps;
            StepNs::DataStoreClass dataStore;
            RunState runState;
            std::filesystem::path scriptPath;
            // shared_ptr : un std::atomic n'est pas déplaçable, alors que PipelineClass doit l'être (buildPipeline la renvoie).
            std::shared_ptr<std::atomic<bool>> pauseRequested = std::make_shared<std::atomic<bool>>(false);

    };

}
