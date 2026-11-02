// include/Backend/core/pipeline/run_state.h
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace PipelineNs {

    using Json = nlohmann::json;

    enum class RunStatus {
        Idle,
        Running,
        Paused,
        Failed,
        Completed
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(RunStatus, {{RunStatus::Idle, "idle"},
                                            {RunStatus::Running, "running"},
                                            {RunStatus::Paused, "paused"},
                                            {RunStatus::Failed, "failed"},
                                            {RunStatus::Completed, "completed"}})

    // Champ "run" du script, à côté de "pipeline" :
    //   { "pipeline": { "steps": [...] },
    //     "run": { "status": "failed", "completedSteps": ["import", "phase-correlation"],
    //              "skip": true, "from": "", "checkpointDir": "output/checkpoints" } }
    //
    //  - ÉTAT, écrit par le pipeline après chaque step : status, completedSteps.
    //  - CONSIGNES, écrites par l'appelant (CLI, GUI) et remises à leur défaut au démarrage du run :
    //      skip  true : on saute les steps déjà faits dont le checkpoint est valide ; false : on repart de zéro ;
    //      from  nom du step où (re)démarrer ; il ne peut pas être après le premier step à refaire.
    //  - checkpointDir : dossier des checkpoints (voir core/CheckpointClass.h). Vide (défaut) : pas de checkpoint,
    //    donc pas de reprise, chaque run repart du premier step.
    struct RunState {
        RunStatus status = RunStatus::Idle;
        std::vector<std::string> completedSteps;   // préfixe du pipeline, dans l'ordre
        bool skipCompletedSteps = true;
        std::string startFromStep;
        std::string checkpointDirectory;

        // Lit le champ "run" d'un script. Lève std::invalid_argument si un champ a le mauvais type
        // ou si le statut est inconnu (sans ce contrôle, un statut inconnu deviendrait "idle" sans erreur).
        static RunState fromJson(const Json& runJson);
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunState, status, completedSteps, skipCompletedSteps, startFromStep, checkpointDirectory)

}
