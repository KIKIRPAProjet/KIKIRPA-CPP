#include "core/Pipeline.h"
#include "core/Checkpoint.h"
#include "core/Config.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

namespace {

std::string stepName(const Step& s) {
    return s.getConfig().is_object() ? s.getConfig().value("name", "") : "";
}

void report(const Pipeline::Logger& log, const std::string& msg, bool error) {
    if (log) log(msg);
    else if (error) std::cerr << msg << '\n';
}

// Index du step où démarrer :
//  - valid = nombre de steps de tête déjà faits (run.completedSteps), de même nom, dont le checkpoint est complet
//    et à jour (mêmes options). Dès qu'un step est invalide, lui et tous les suivants sont à refaire ;
//  - run.from permet de revenir plus tôt (refaire un step), jamais d'aller au-delà de valid (données manquantes).
size_t resolveStart(const std::vector<std::unique_ptr<Step>>& steps, const RunState& run, const Checkpoint& cp,
                    const Pipeline::Logger& log) {
    size_t valid = 0;
    while (valid < run.completedSteps.size() && valid < steps.size() &&
           run.completedSteps[valid] == stepName(*steps[valid]) && cp.valid(valid, *steps[valid]))
        ++valid;
    if (cp.enabled() && valid < run.completedSteps.size())
        report(log, "checkpoint of step " + std::to_string(valid + 1) + " ('" + run.completedSteps[valid] +
                        "') is missing or outdated: resuming from there", false);

    if (run.from.empty()) return valid;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (stepName(*steps[i]) != run.from) continue;
        if (i > valid)
            throw std::invalid_argument("cannot start at '" + run.from + "': step " + std::to_string(valid + 1) +
                                        " ('" + stepName(*steps[valid]) + "') must be done first");
        return i;
    }
    throw std::invalid_argument("run.from: unknown step '" + run.from + "'");
}

} // namespace

void Pipeline::configure(const Json& cfg) {
    if (!cfg.is_object() || !cfg.contains("pipeline") || !cfg.at("pipeline").is_object() ||
        !cfg.at("pipeline").contains("steps") || !cfg.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("expected { \"pipeline\": { \"steps\": [...] } }");
    const Json& stepsJson = cfg.at("pipeline").at("steps");
    if (stepsJson.size() != steps.size())
        throw std::invalid_argument("pipeline JSON has " + std::to_string(stepsJson.size()) +
                                    " steps, expected " + std::to_string(steps.size()));
    for (size_t i = 0; i < steps.size(); ++i) {
        try {
            steps[i]->importConfig(stepsJson.at(i));
        } catch (const std::exception& e) {
            throw std::invalid_argument("pipeline.steps[" + std::to_string(i) + "]: " + e.what());
        }
    }
    if (cfg.contains("run")) runState = parseRunState(cfg.at("run"));
}

Json Pipeline::exportConfig() const {
    PipelineConfig config;
    for (const auto& s : steps)
        config.steps.push_back(s->getConfig().is_object() ? s->getConfig().get<StepConfig>() : StepConfig{});
    return {{"pipeline", config}};
}

Json Pipeline::exportScript() const {
    Json script = exportConfig();
    script["run"] = runState;
    return script;
}

// Écriture atomique (fichier temporaire puis rename). Un échec n'arrête pas le pipeline, il est seulement signalé.
void Pipeline::saveScript(const Logger& log) const {
    if (scriptPath.empty()) return;
    try {
        if (scriptPath.has_parent_path()) fs::create_directories(scriptPath.parent_path());
        const fs::path tmp = scriptPath.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary);
            out << exportScript().dump(2) << '\n';
            if (!out) throw std::runtime_error("write failed");
        }
        fs::rename(tmp, scriptPath);
    } catch (const std::exception& e) {
        report(log, "warning: cannot write " + scriptPath.string() + ": " + e.what(), true);
    }
}

bool Pipeline::run(const Logger& log) {
    const size_t n = steps.size();
    const Checkpoint cp(runState.checkpointDir);

    // 1. Où démarrer ?
    size_t start = 0;
    if (runState.skip) start = resolveStart(steps, runState, cp, log);
    else if (!runState.from.empty()) report(log, "run.from ignored: skip=false restarts from the first step", false);
    if (start > 0 && start < n) report(log, "resuming at step " + std::to_string(start + 1) + "/" + std::to_string(n), false);

    // 2. État propre : les checkpoints des steps à refaire sont périmés, les consignes sont consommées
    cp.removeFrom(start);
    runState.completedSteps.resize(start);
    runState.skip = true;
    runState.from.clear();
    data = DataStore();

    // 3. Restauration des sorties des steps sautés (dernière version de chaque clé)
    std::map<std::string, std::pair<size_t, std::string>> latest;   // clé -> (step producteur, type)
    for (size_t i = 0; i < start; ++i)
        for (const PipelineOption& out : steps[i]->getConfig().get<StepConfig>().signature.pipelineOptions)
            latest[out.key] = {i, out.type};
    try {
        for (const auto& [key, src] : latest) cp.load(src.first, *steps[src.first], key, src.second, data);
    } catch (const std::exception& e) {
        report(log, std::string("cannot restore checkpoint: ") + e.what(), true);
        runState.status = RunStatus::Failed;
        saveScript(log);
        return false;
    }

    // 4. Exécution
    runState.status = RunStatus::Running;
    saveScript(log);
    for (size_t i = start; i < n; ++i) {
        const auto& s = steps[i];
        const std::string prefix = "Step " + std::to_string(i + 1) + "/" + std::to_string(n);

        if (pauseRequested->exchange(false)) {   // pause demandée : arrêt propre entre deux steps
            runState.status = RunStatus::Paused;
            saveScript(log);
            report(log, prefix + " paused", false);
            return false;
        }

        s->setDataStore(data);
        bool ok = false;
        std::string error;
        try {
            ok = s->execute();
        } catch (const std::exception& e) {   // une exception d'un step = un step en échec, pas un crash
            error = e.what();
        }
        if (!ok) {
            report(log, prefix + " failed: " + (error.empty() ? s->exportData().dump() : error), true);
            runState.status = RunStatus::Failed;
            saveScript(log);
            return false;   // reprise possible : completedSteps et checkpoints sont à jour
        }

        // Step réussi : on sauvegarde ses sorties PUIS on le déclare fait (jamais l'inverse)
        std::string why;
        if (cp.enabled() && !cp.save(i, *s, data, why))
            report(log, prefix + " warning: checkpoint incomplete (" + why + "), this step will be redone on resume", false);
        runState.completedSteps.push_back(stepName(*s));
        saveScript(log);
        report(log, prefix + " OK " + s->exportData().dump(), false);
    }
    if (start == n && n > 0) report(log, "nothing to do: all steps are already done", false);
    runState.status = RunStatus::Completed;
    saveScript(log);
    return true;
}

} // namespace stitching
