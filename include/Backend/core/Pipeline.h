#pragma once
#include "core/DataStore.h"
#include "core/RunState.h"
#include "core/Step.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stitching {

class Pipeline {
    std::vector<std::unique_ptr<Step>> steps;   // Pipeline --> Steps
    DataStore data;                             // sorties des steps déjà exécutés
    RunState runState;                          // où en est le run (voir core/RunState.h)
    std::filesystem::path scriptPath;           // si non vide : le script (pipeline + run) y est réécrit après chaque step
    std::shared_ptr<std::atomic<bool>> stopRequested = std::make_shared<std::atomic<bool>>(false); // shared_ptr : Pipeline reste déplaçable

    std::string stepName(size_t i) const;
    void saveScript(const std::function<void(const std::string&)>& log) const;
public:
    using Logger = std::function<void(const std::string&)>; // Logger pour afficher les messages d'exécution

    Pipeline() = default;
    // Lit { "pipeline": { "steps": [...] }, "run": {...} } : steps[i] du JSON → i-ème step ("run" est optionnel)
    void configure(const Json& cfg);
    // Écrit uniquement { "pipeline": ... } (configuration seule)
    Json exportConfig() const;
    // Écrit { "pipeline": ..., "run": ... } : le script complet, qui permet de reprendre
    Json exportScript() const;
    void add(std::unique_ptr<Step> s) { steps.push_back(std::move(s)); }

    // Exécute les steps dans l'ordre, retourne false si un step échoue ou si une pause a été demandée.
    // Selon run : saute les steps déjà faits (checkpoints valides), ou repart de "from", ou de zéro (skip=false).
    // Lève std::invalid_argument si "from" est inconnu ou dépasse le premier step non fait.
    bool run(const Logger& log = {});

    // Demande une pause : prise en compte entre deux steps (un step en cours va jusqu'au bout). Thread-safe.
    void requestPause() { stopRequested->store(true); }

    void setRunState(const RunState& r) { runState = r; }
    const RunState& getRunState() const { return runState; }
    void setScriptPath(std::filesystem::path p) { scriptPath = std::move(p); }

    const DataStore& getData() const { return data; }
    // Liste des steps, dans l'ordre d'exécution
    const std::vector<std::unique_ptr<Step>>& getSteps() const { return steps; }
};

} // namespace stitching
