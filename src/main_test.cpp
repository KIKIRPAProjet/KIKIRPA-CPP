#include "core/PipelineBuilder.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace stitching;

// Usage : stitching_test [script.json] [--from <step>] [--restart]
//   sans argument  : lance pipeline.json depuis la racine du projet
//   --from <step>  : (re)démarre à ce step (les steps d'avant doivent avoir un checkpoint valide)
//   --restart      : ignore les checkpoints et repart de zéro
// L'état du run est réécrit après chaque step dans output/run.json : pour reprendre après un arrêt ou un crash,
// relancer avec  stitching_test output/run.json
int main(int argc, char** argv) {
    std::string script, from;
    bool restart = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--restart") restart = true;
        else if (a == "--from" && i + 1 < argc) from = argv[++i];
        else script = a;
    }

    // Sans script : on se place à la racine du projet (pipeline.json, images/) -> le chemin par défaut est la racine du projet
    if (script.empty()) {
        std::filesystem::current_path(PROJECT_DIR);
        script = "pipeline.json";
    }
    std::ifstream file(script);
    if (!file) {
        std::cerr << "cannot open " << script << '\n';
        return 1;
    }

    try {
        Pipeline p = buildPipeline(Json::parse(file)); // On lui donne le JSON du pipeline (JSON::parse(file) transformation en objet JSON)
        const auto& steps = p.getSteps();   // liste des steps lus dans le JSON

        for (size_t i = 0; i < steps.size(); ++i) {
            const Json& cfg = steps[i]->getConfig();
            std::cout << i + 1 << ". " << cfg.at("name").get<std::string>();
            for (const Json& o : cfg.at("options"))
                std::cout << "  " << o.at("key").get<std::string>() << '=' << o.at("value");
            std::cout << '\n';
        }

        // Consignes de la ligne de commande (le frontend les écrira directement dans "run")
        RunState run = p.getRunState();
        if (restart) run.skip = false;
        if (!from.empty()) run.from = from;
        p.setRunState(run);
        p.setScriptPath("output/run.json");   // état écrit après chaque step -> reprise possible

        const bool ok = p.run([](const std::string& m) { std::cout << "  " << m << '\n'; }); // un step en échec est déjà signalé sur stderr
        std::cout << (ok ? "Pipeline OK" : "Pipeline KO") << '\n';

        return ok ? 0 : 1;
    } catch (const Json::parse_error& e) {
        std::cerr << script << ": invalid JSON: " << e.what() << '\n';
        return 1;
    } catch (const std::invalid_argument& e) {
        std::cerr << script << ": invalid pipeline configuration: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << script << ": " << e.what() << '\n';
        return 1;
    }
}
