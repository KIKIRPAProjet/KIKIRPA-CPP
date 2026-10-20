#include "core/Pipeline.h"
#include "core/Checkpoint.h"
#include "core/Config.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

std::string Pipeline::stepName(size_t i) const {
    return steps[i]->getConfig().is_object() ? steps[i]->getConfig().value("name", "") : "";
}

void Pipeline::configure(const Json& cfg) {
    if (!cfg.is_object())
        throw std::invalid_argument("pipeline script must be a JSON object");
    if (!cfg.contains("pipeline") || !cfg.at("pipeline").is_object())
        throw std::invalid_argument("field 'pipeline' must be an object");
    if (!cfg.at("pipeline").contains("steps") || !cfg.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("field 'pipeline.steps' must be an array");

    const Json& stepConfigs = cfg.at("pipeline").at("steps");
    if (stepConfigs.size() != steps.size())
        throw std::invalid_argument("pipeline JSON has " + std::to_string(stepConfigs.size()) +
                                    " steps, expected " + std::to_string(steps.size()));
    for (size_t i = 0; i < steps.size(); ++i) {
        try {
            steps[i]->importConfig(stepConfigs.at(i));
        } catch (const std::exception& e) {
            throw std::invalid_argument("pipeline.steps[" + std::to_string(i) + "]: " + e.what());
        }
    }
    if (cfg.contains("run")) {
        validateRunStateJson(cfg.at("run"));
        try {
            runState = cfg.at("run").get<RunState>();
        } catch (const std::exception& e) {
            throw std::invalid_argument(std::string("field 'run': ") + e.what());
        }
    }
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

// Écriture atomique (fichier temporaire puis rename) : un crash ne laisse jamais un script tronqué.
// Un échec d'écriture n'arrête pas le traitement, il est seulement signalé.
void Pipeline::saveScript(const Logger& log) const {
    if (scriptPath.empty()) return;
    try {
        if (scriptPath.has_parent_path()) fs::create_directories(scriptPath.parent_path());
        const fs::path tmp = scriptPath.string() + ".tmp";
        { std::ofstream out(tmp); out << exportScript().dump(2); if (!out) throw std::runtime_error("write failed"); }
        fs::rename(tmp, scriptPath);
    } catch (const std::exception& e) {
        const std::string msg = "warning: cannot write " + scriptPath.string() + ": " + e.what();
        std::cerr << msg << '\n';
        if (log) log(msg);
    }
}

namespace {

// Index du step où démarrer, d'après run.completedSteps, les checkpoints et run.from.
//  - "valid" = nombre de steps de tête dont le nom correspond ET dont le checkpoint est complet et à jour
//    (options inchangées). Dès qu'un step est invalide, lui et tous les suivants sont à refaire.
//  - run.from permet de revenir plus tôt (refaire un step), jamais de sauter au-delà de "valid" (données manquantes).
size_t resolveStart(const std::vector<std::unique_ptr<Step>>& steps, const RunState& run, const Checkpoint& cp,
                    const Pipeline::Logger& log) {
    size_t valid = 0;
    while (valid < run.completedSteps.size() && valid < steps.size() &&
           run.completedSteps[valid] == steps[valid]->getConfig().value("name", "") &&
           cp.valid(valid, *steps[valid]))
        ++valid;
    if (log && valid < run.completedSteps.size())
        log("checkpoint of step " + std::to_string(valid + 1) + " ('" + run.completedSteps[valid] +
            "') is missing or outdated: resuming from there");

    if (run.from.empty()) return valid;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i]->getConfig().value("name", "") != run.from) continue;
        if (i > valid)
            throw std::invalid_argument("cannot start at '" + run.from + "': step " + std::to_string(valid + 1) +
                                        " must be completed first");
        return i;
    }
    throw std::invalid_argument("run.from: unknown step '" + run.from + "'");
}

} // namespace

// L'exécution séquentielle, avec reprise
bool Pipeline::run(const Logger& log) {
    const size_t n = steps.size();
    const Checkpoint cp(runState.checkpointDir);
    stopRequested->store(false);

    // 1. Où démarrer ?
    size_t start = 0;
    if (runState.skip) start = resolveStart(steps, runState, cp, log);
    else if (!runState.from.empty() && log) log("run.from ignored: skip=false restarts from the beginning");
    if (start > 0 && start < n && log) log("resuming at step " + std::to_string(start + 1) + "/" + std::to_string(n));

    // 2. État propre : les checkpoints des steps à refaire sont périmés
    cp.removeFrom(start);
    runState.completedSteps.resize(start);
    data = DataStore();
    runState.skip = true;     // consignes consommées : le script réécrit reprendra là où on en est
    runState.from.clear();

    // 3. Restauration du DataStore à partir des steps déjà faits (dernière version de chaque clé)
    std::map<std::string, std::pair<size_t, std::string>> latest;   // clé -> (step producteur, type)
    for (size_t i = 0; i < start; ++i)
        for (const PipelineOption& out : steps[i]->getConfig().get<StepConfig>().signature.pipelineOptions)
            latest[out.key] = {i, out.type};
    try {
        for (const auto& [key, src] : latest) cp.load(src.first, *steps[src.first], key, src.second, data);
    } catch (const std::exception& e) {
        const std::string msg = std::string("cannot restore checkpoint: ") + e.what();
        std::cerr << msg << '\n';
        if (log) log(msg);
        runState.status = RunStatus::Failed;
        saveScript(log);
        return false;
    }

    // 4. Exécution
    runState.status = RunStatus::Running;
    saveScript(log);
    for (size_t i = start; i < n; ++i) { // On boucle sur les steps restants
        const auto& s = steps[i]; // Le step courant
        const std::string prefix = "Step " + std::to_string(i + 1) + "/" + std::to_string(n);

        if (stopRequested->load()) {   // pause demandée : on s'arrête proprement entre deux steps
            runState.status = RunStatus::Paused;
            saveScript(log);
            if (log) log(prefix + " paused");
            return false;
        }

        s->setDataStore(data); // Association du DataStore au step courant
        bool ok = false;
        std::string error;
        try {
            ok = s->execute();
        } catch (const std::exception& e) {   // un step qui lève une exception = un step en échec
            error = e.what();
        }
        if (!ok) { // si échec on sort de la boucle et on retourne false -> on arrête la pipeline
            const std::string msg = prefix + " failed: " + (error.empty() ? s->exportData().dump() : error);
            std::cerr << msg << '\n';
            if (log) log(msg);
            runState.status = RunStatus::Failed;
            saveScript(log);
            return false;   // reprise possible : completedSteps + checkpoints sont à jour
        }

        // Step réussi : on sauvegarde ses sorties PUIS on le déclare terminé (jamais l'inverse)
        std::string why;
        if (!cp.save(i, *s, data, why) && log)
            log(prefix + " warning: checkpoint incomplete (" + why + "), this step will be redone on resume");
        runState.completedSteps.push_back(stepName(i));
        saveScript(log);
        if (log) log(prefix + " OK " + s->exportData().dump());
    }
    if (start == n && log) log("nothing to do: all steps are already completed");
    runState.status = RunStatus::Completed;
    saveScript(log);
    return true;
}

} // namespace stitching
