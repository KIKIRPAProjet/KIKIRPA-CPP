// run_pipeline.cpp
// Lance un pipeline décrit par un script JSON (voir core/Config.h, core/RunState.h et examples/pipeline.json).
//
//   stitching_run <script.json> [--checkpoints <dossier>] [--from <step>] [--restart]
//
//   --checkpoints <dossier>  sauvegarde les sorties de chaque step dans <dossier> et y écrit l'état du run
//                            (<dossier>/run.json). Pour reprendre après un échec ou un arrêt :
//                              stitching_run <dossier>/run.json
//   --from <step>            (re)démarre à ce step ; les steps d'avant doivent avoir un checkpoint valide
//   --restart                ignore les checkpoints et repart du premier step
//
// Code de retour : 0 si tous les steps réussissent, 1 sinon (script invalide, step en échec).
#include "core/PipelineBuilder.h"
#include <filesystem>
#include <iostream>
#include <string>

using namespace stitching;

int main(int argc, char** argv) {
    std::string script, from, checkpoints;
    bool restart = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--restart") restart = true;
        else if (a == "--from" && i + 1 < argc) from = argv[++i];
        else if (a == "--checkpoints" && i + 1 < argc) checkpoints = argv[++i];
        else if (script.empty() && a.rfind("--", 0) != 0) script = a;
        else {
            std::cerr << "usage: " << argv[0] << " <script.json> [--checkpoints <dossier>] [--from <step>] [--restart]\n";
            return 1;
        }
    }
    if (script.empty()) {
        std::cerr << "usage: " << argv[0] << " <script.json> [--checkpoints <dossier>] [--from <step>] [--restart]\n";
        return 1;
    }

    try {
        Pipeline p = buildPipelineFromFile(script);
        const auto& steps = p.getSteps();
        for (size_t i = 0; i < steps.size(); ++i) {
            const Json& cfg = steps[i]->getConfig();
            std::cout << i + 1 << ". " << cfg.value("name", "?");
            for (const Json& o : cfg.at("options")) std::cout << "  " << o.value("key", "") << '=' << o.at("value");
            std::cout << '\n';
        }

        // Consignes de la ligne de commande (la GUI les écrira directement dans "run")
        RunState run = p.getRunState();
        if (!checkpoints.empty()) run.checkpointDir = checkpoints;
        if (restart) run.skip = false;
        if (!from.empty()) run.from = from;
        p.setRunState(run);
        if (!run.checkpointDir.empty()) p.setScriptPath(std::filesystem::path(run.checkpointDir) / "run.json");

        const bool ok = p.run([](const std::string& m) { std::cout << "  " << m << '\n'; });
        std::cout << (ok ? "Pipeline OK" : "Pipeline KO") << '\n';
        if (!ok && !run.checkpointDir.empty())
            std::cout << "Pour reprendre : " << argv[0] << ' ' << (std::filesystem::path(run.checkpointDir) / "run.json").string() << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
