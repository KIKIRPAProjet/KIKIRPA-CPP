// include/Backend/core/pipeline/pipeline.h
#pragma once

#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    using Json = nlohmann::json;

    class PipelineClass {

        public:
            using Logger = std::function<void(const std::string&)>;

            PipelineClass() = default;
            ~PipelineClass() = default;

            // Lit { "pipeline": { "steps": [...] }, "run": {...} } : steps[i] du JSON → i-ème step ; "run" est facultatif
            void configure(const Json& scriptJson);
            // { "pipeline": ... } : la configuration seule
            Json exportConfig() const;
            // { "pipeline": ..., "run": ... } : le script complet, qui permet de reprendre
            Json exportScript() const;
            void addStep(std::unique_ptr<StepNs::StepClass> step) { steps.push_back(std::move(step)); }

            // Exécute les steps dans l'ordre. Renvoie false si un step échoue (false ou exception) ou si une pause a été
            // demandée. Avec des checkpoints (run.checkpointDir non vide) et run.skip, saute les steps déjà faits dont le
            // checkpoint est valide, et restaure leurs sorties ; run.from permet de redémarrer plus tôt.
            // Lève std::invalid_argument si run.from est inconnu ou après le premier step à refaire.
            // Messages : `log` s'il est fourni, sinon std::cerr pour les échecs.
            bool run(const Logger& log = {});

            // Demande une pause, prise en compte entre deux steps (le step en cours va jusqu'au bout). Thread-safe.
            void requestPause() { pauseRequested->store(true); }

            void setRunState(const RunState& newRunState) { runState = newRunState; }
            const RunState& getRunState() const { return runState; }
            // Fichier où le script complet (exportScript) est réécrit après chaque step ; vide : pas d'écriture
            void setScriptPath(std::filesystem::path newScriptPath) { scriptPath = std::move(newScriptPath); }

            const StepNs::DataStoreClass& getDataStore() const { return dataStore; }
            // Steps dans l'ordre d'exécution
            const std::vector<std::unique_ptr<StepNs::StepClass>>& getSteps() const { return steps; }

        private:
            void saveScript(const Logger& log) const;

            std::vector<std::unique_ptr<StepNs::StepClass>> steps;   // Pipeline --> Steps
            StepNs::DataStoreClass dataStore;                        // sorties des steps déjà exécutés
            RunState runState;                                       // où en est le run (voir core/RunState.h)
            std::filesystem::path scriptPath;                        // non vide : le script complet y est réécrit après chaque step
            // shared_ptr : un std::atomic n'est pas déplaçable, et PipelineClass doit l'être (buildPipeline le renvoie)
            std::shared_ptr<std::atomic<bool>> pauseRequested = std::make_shared<std::atomic<bool>>(false);

    };

}
