// include/Backend/core/pipeline/run_state.h
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace PipelineNs {

    using Json = nlohmann::json;

    // Statut d'un run : Idle (jamais lancé), Running, Paused (pause demandée), Failed, Completed.
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
    //   "run": { "status": "failed", "completedSteps": ["import"], "skipCompletedSteps": true,
    //            "startFromStep": "", "checkpointDirectory": "output/checkpoints" }
    // - État, écrit par le pipeline après chaque step : status, completedSteps.
    // - Consignes, écrites par l'appelant (CLI, GUI) et remises à leur valeur par défaut au démarrage du run :
    //   skipCompletedSteps, startFromStep.
    struct RunState {
        RunStatus status = RunStatus::Idle;
        std::vector<std::string> completedSteps;   // noms des steps terminés : un préfixe du pipeline, dans l'ordre
        bool skipCompletedSteps = true;            // vrai : saute les steps déjà faits dont le checkpoint est valide ; faux : repart du premier
        std::string startFromStep;                 // nom du step où (re)démarrer ; ne peut pas être après le premier step à refaire
        std::string checkpointDirectory;           // dossier des checkpoints ; vide : pas de checkpoint, donc pas de reprise

        // Lit le champ "run" d'un script. Lève std::invalid_argument si un champ a le mauvais type ou si le statut
        // est inconnu (sans ce contrôle, un statut inconnu deviendrait "idle" sans erreur).
        static RunState fromJson(const Json& runJson);
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunState, status, completedSteps, skipCompletedSteps, startFromStep, checkpointDirectory)

}
