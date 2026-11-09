#include "core/pipeline/pipeline_builder.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

    int runFrontend(int, char**) {
        // Le point d'entrée du frontend sera branché ici lorsqu'il sera disponible sur cette branche.
        return EXIT_SUCCESS;
    }

    int runPipeline(const std::string& scriptPath) {
        try {
            PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(scriptPath);
            const bool succeeded = pipeline.run([](const std::string& message) {
                std::cout << message << '\n';
            });
            return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& exception) {
            std::cerr << "Pipeline error: " << exception.what() << '\n';
            return EXIT_FAILURE;
        }
    }

}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--pipeline") {
        if (argc != 3 || argv[2][0] == '\0') {
            std::cerr << "Usage: stitching --pipeline <script.json>\n";
            return 2;
        }
        return runPipeline(argv[2]);
    }

    return runFrontend(argc, argv);
}
