// include/backend/core/pipeline/run_state.h
#pragma once

// NOLINTBEGIN(misc-include-cleaner)
#include <nlohmann/json.hpp>
// NOLINTEND(misc-include-cleaner)

#include <cstdint>
#include <string>
#include <vector>

namespace PipelineNs {

    // The JSON macros need the full nlohmann header, which include-cleaner cannot
    // associate with their generated declarations.
    // NOLINTBEGIN(misc-include-cleaner)
    using Json = nlohmann::json;

    // Statut d'un run : Idle (jamais lancé), Running, Paused (pause demandée), Failed, Completed.
    enum class RunStatus : std::uint8_t {
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
        Json stepResults = Json::object();          // résultats disponibles par nom de step
        std::string failedStep;                     // nom du dernier step en échec, s'il y en a un
        std::string error;                          // dernier message d'erreur du pipeline
        bool skipCompletedSteps = true;            // vrai : saute les steps déjà faits dont le checkpoint est valide ; faux : repart du premier
        std::string startFromStep;                 // nom du step où (re)démarrer ; ne peut pas être après le premier step à refaire
        std::string checkpointDirectory;           // dossier des checkpoints ; vide : pas de checkpoint, donc pas de reprise

        // Lit le champ "run" d'un script. Lève std::invalid_argument si un champ a le mauvais type ou si le statut
        // est inconnu (sans ce contrôle, un statut inconnu deviendrait "idle" sans erreur).
        static RunState fromJson(const Json& runJson);
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunState, status, completedSteps, stepResults, failedStep, error, skipCompletedSteps, startFromStep, checkpointDirectory)
    // NOLINTEND(misc-include-cleaner)

}
