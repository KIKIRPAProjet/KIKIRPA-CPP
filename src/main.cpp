#include "core/PipelineBuilder.h"
#include <fstream>
#include <iostream>

using namespace stitching;

int main(int argc, char** argv) {

    const std::string script = argc > 1 ? argv[1] : "pipeline.json";
    std::ifstream file(script);
    if (!file) {
        std::cerr << "cannot open " << script << '\n';
        return 1;
    }

    try {
        Pipeline p = buildPipeline(Json::parse(file));
        const auto& steps = p.getSteps();   // liste des steps lus dans le JSON

        for (size_t i = 0; i < steps.size(); ++i) {
            const Json& cfg = steps[i]->getConfig();
            std::cout << i + 1 << ". " << cfg.at("name").get<std::string>();
            for (const Json& o : cfg.at("options"))
                std::cout << "  " << o.at("key").get<std::string>() << '=' << o.at("value");
            std::cout << '\n';
        }

        const bool ok = p.run();   // un step en échec est déjà signalé sur stderr
        std::cout << (ok ? "Pipeline OK" : "Pipeline KO") << '\n';

        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << script << ": " << e.what() << '\n';
        return 1;
    }
}
