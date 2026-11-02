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
#include "core/pipeline/pipeline_builder.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace PipelineNs;

namespace {

    int printUsage(const char* programName) {
        std::cerr << "usage: " << programName << " <script.json> [--checkpoints <dossier>] [--from <step>] [--restart]\n";
        return 1;
    }

}

int main(int argc, char** argv) {
    std::string scriptPath, startFromStep, checkpointDirectory;
    bool restart = false;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        const std::string argument = argv[argumentIndex];
        if (argument == "--restart") restart = true;
        else if (argument == "--from" && argumentIndex + 1 < argc) startFromStep = argv[++argumentIndex];
        else if (argument == "--checkpoints" && argumentIndex + 1 < argc) checkpointDirectory = argv[++argumentIndex];
        else if (scriptPath.empty() && argument.rfind("--", 0) != 0) scriptPath = argument;
        else return printUsage(argv[0]);
    }
    if (scriptPath.empty()) return printUsage(argv[0]);

    try {
        PipelineClass pipeline = buildPipelineFromFile(scriptPath);
        const auto& steps = pipeline.getSteps();
        for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
            const Json& stepConfigJson = steps[stepIndex]->getConfigJson();
            std::cout << stepIndex + 1 << ". " << stepConfigJson.value("name", "?");
            for (const Json& parameterJson : stepConfigJson.at("parameters"))
                std::cout << "  " << parameterJson.value("name", "") << '=' << parameterJson.at("value");
            std::cout << '\n';
        }

        // Consignes de la ligne de commande (la GUI les écrira directement dans "run")
        RunState runState = pipeline.getRunState();
        if (!checkpointDirectory.empty()) runState.checkpointDirectory = checkpointDirectory;
        if (restart) runState.skipCompletedSteps = false;
        if (!startFromStep.empty()) runState.startFromStep = startFromStep;
        pipeline.setRunState(runState);
        if (!runState.checkpointDirectory.empty()) pipeline.setScriptPath(std::filesystem::path(runState.checkpointDirectory) / "run.json");

        const bool succeeded = pipeline.run([](const std::string& message) { std::cout << "  " << message << '\n'; });
        std::cout << (succeeded ? "Pipeline OK" : "Pipeline KO") << '\n';
        if (!succeeded && !runState.checkpointDirectory.empty())
            std::cout << "Pour reprendre : " << argv[0] << ' ' << (std::filesystem::path(runState.checkpointDirectory) / "run.json").string() << '\n';
        return succeeded ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
