#include "core/PipelineBuilder.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace stitching;

int main(int argc, char** argv) {
    const std::filesystem::path script = argc > 1
        ? std::filesystem::absolute(argv[1])
        : std::filesystem::path(PROJECT_DIR) / "pipeline.json";
    std::ifstream file(script);
    if (!file) {
        std::cerr << "cannot open " << script.string() << '\n';
        return 1;
    }

    try {
        Pipeline pipeline = buildPipeline(Json::parse(file));
        std::filesystem::current_path(script.parent_path());
        const auto& steps = pipeline.getSteps();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const Json& config = steps[i]->getConfig();
            std::cout << i + 1 << ". " << config.at("name").get<std::string>();
            for (const Json& option : config.at("options"))
                std::cout << "  " << option.at("key").get<std::string>()
                          << '=' << option.at("value");
            std::cout << '\n';
        }

        const bool ok = pipeline.run();
        std::cout << (ok ? "Pipeline OK" : "Pipeline KO") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << script.string() << ": " << e.what() << '\n';
        return 1;
    }
}
