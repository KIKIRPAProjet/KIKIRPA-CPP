// run_pipeline.cpp
// Lance un pipeline décrit par un script JSON (voir core/Config.h et examples/pipeline.json).
//
//   stitching_run <script.json>
//
// Code de retour : 0 si tous les steps réussissent, 1 sinon (script invalide ou step en échec).
#include "core/PipelineBuilder.h"
#include <iostream>
#include <string>

using namespace stitching;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <script.json>\n";
        return 1;
    }

    try {
        Pipeline p = buildPipelineFromFile(argv[1]);
        const auto& steps = p.getSteps();
        for (size_t i = 0; i < steps.size(); ++i) {
            const Json& cfg = steps[i]->getConfig();
            std::cout << i + 1 << ". " << cfg.value("name", "?");
            for (const Json& o : cfg.at("options")) std::cout << "  " << o.value("key", "") << '=' << o.at("value");
            std::cout << '\n';
        }

        const bool ok = p.run([](const std::string& m) { std::cout << "  " << m << '\n'; });
        std::cout << (ok ? "Pipeline OK" : "Pipeline KO") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
