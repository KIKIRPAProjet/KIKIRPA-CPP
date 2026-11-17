#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "mainwindow.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int EXIT_STEP_FAILED = 1;
constexpr int EXIT_INVALID_USAGE = 2;
constexpr int EXIT_IO_ERROR = 3;

constexpr const char* USAGE =
    "Usage:\n"
    "  stitching                                    open the graphical interface\n"
    "  stitching --pipeline <script.json> [--resume] run a pipeline script\n"
    "  stitching --help                             show this help\n"
    "\n"
    "Relative paths in the script are read from the script's directory.\n"
    "--resume continues the previous run from its last completed step and needs\n"
    "run.checkpointDirectory in the script. Without --resume, every step is run again.\n"
    "\n"
    "Exit codes: 0 success, 1 a step failed, 2 invalid command line or script, 3 file error.\n";

struct CommandLine {
    bool helpRequested = false;
    bool resumeRequested = false;
    std::string scriptPath;
    std::string error;
};

class FileErrorClass : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool isCommandLineMode(int argumentCount, char** arguments) {
    if (argumentCount < 2) return false;
    const std::string firstArgument = arguments[1];
    return firstArgument.rfind("--", 0) == 0 || firstArgument == "-h";
}

CommandLine parseCommandLine(int argumentCount, char** arguments) {
    CommandLine commandLine;
    for (int argumentIndex = 1; argumentIndex < argumentCount && commandLine.error.empty(); ++argumentIndex) {
        const std::string argument = arguments[argumentIndex];
        if (argument == "--help" || argument == "-h") {
            commandLine.helpRequested = true;
        } else if (argument == "--resume") {
            commandLine.resumeRequested = true;
        } else if (argument == "--pipeline") {
            const bool hasScript = argumentIndex + 1 < argumentCount && arguments[argumentIndex + 1][0] != '\0';
            if (hasScript) commandLine.scriptPath = arguments[++argumentIndex];
            else commandLine.error = "--pipeline needs a script file";
        } else {
            commandLine.error = "unknown option '" + argument + "'";
        }
    }
    if (commandLine.error.empty() && !commandLine.helpRequested && commandLine.scriptPath.empty())
        commandLine.error = "missing --pipeline <script.json>";
    return commandLine;
}

fs::path enterScriptDirectory(const std::string& scriptArgument) {
    std::error_code errorCode;
    const fs::path scriptFile = fs::absolute(fs::path(scriptArgument), errorCode);
    if (errorCode || !std::ifstream(scriptFile).good()) throw FileErrorClass("cannot open " + scriptArgument);
    fs::current_path(scriptFile.parent_path(), errorCode);
    if (errorCode)
        throw FileErrorClass("cannot enter the script directory " + scriptFile.parent_path().string() + ": " +
                             errorCode.message());
    return scriptFile;
}

fs::path runStatePath(const PipelineNs::RunState& runState) {
    return fs::absolute(fs::path(runState.checkpointDirectory)) / "run.json";
}

Json readJsonFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw FileErrorClass("cannot open " + path.string());
    try {
        return Json::parse(input);
    } catch (const Json::parse_error& exception) {
        throw FileErrorClass("invalid JSON in " + path.string() + ": " + exception.what());
    }
}

std::vector<std::string> previousCompletedSteps(const PipelineNs::RunState& runState) {
    if (runState.checkpointDirectory.empty())
        throw std::invalid_argument("--resume needs run.checkpointDirectory in the script");
    const fs::path statePath = runStatePath(runState);
    if (!fs::exists(statePath)) {
        std::cout << "no previous run in " << statePath.string() << ": starting from the first step\n";
        return {};
    }
    const Json stateJson = readJsonFile(statePath);
    if (!stateJson.contains("run")) return {};
    return PipelineNs::RunState::fromJson(stateJson.at("run")).completedSteps;
}

void prepareRunState(PipelineNs::PipelineClass& pipeline, bool resumeRequested) {
    PipelineNs::RunState runState = pipeline.getRunState();
    runState.status = PipelineNs::RunStatus::Idle;
    runState.completedSteps = resumeRequested ? previousCompletedSteps(runState) : std::vector<std::string>{};
    runState.skipCompletedSteps = true;
    pipeline.setRunState(runState);
    if (!runState.checkpointDirectory.empty()) pipeline.setScriptPath(runStatePath(runState));
}

void reportFailedStep(const PipelineNs::PipelineClass& pipeline, const std::string& scriptArgument) {
    const auto& steps = pipeline.getSteps();
    const std::size_t failedIndex = pipeline.getRunState().completedSteps.size();
    std::cerr << "Pipeline error: ";
    if (failedIndex >= steps.size()) {
        std::cerr << "the run stopped before the end\n";
        return;
    }
    const Json result = steps[failedIndex]->getResult();
    std::cerr << "step " << failedIndex + 1 << "/" << steps.size() << " '"
              << steps[failedIndex]->getConfigJson().value("name", "") << "' failed";
    if (result.is_object() && result.contains("error")) std::cerr << ": " << result.at("error").dump();
    std::cerr << '\n';
    if (!pipeline.getRunState().checkpointDirectory.empty())
        std::cerr << "Fix the cause, then continue with: stitching --pipeline \"" << scriptArgument << "\" --resume\n";
}

bool executePipeline(PipelineNs::PipelineClass& pipeline) {
    try {
        return pipeline.run([](const std::string& message) { std::cout << message << '\n'; });
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const std::exception& exception) {
        std::cerr << "Pipeline error: " << exception.what() << '\n';
        return false;
    }
}

int runPipeline(const CommandLine& commandLine) {
    PipelineNs::PipelineClass pipeline =
        PipelineNs::buildPipelineFromFile(enterScriptDirectory(commandLine.scriptPath));
    prepareRunState(pipeline, commandLine.resumeRequested);
    if (executePipeline(pipeline)) return EXIT_SUCCESS;
    reportFailedStep(pipeline, commandLine.scriptPath);
    return EXIT_STEP_FAILED;
}

int runPipelineCommand(const CommandLine& commandLine) {
    try {
        return runPipeline(commandLine);
    } catch (const FileErrorClass& exception) {
        std::cerr << "Pipeline error: " << exception.what() << '\n';
        return EXIT_IO_ERROR;
    } catch (const std::exception& exception) {
        std::cerr << "Pipeline error: " << exception.what() << '\n';
        return EXIT_INVALID_USAGE;
    }
}

int runCommandLine(int argumentCount, char** arguments) {
    const CommandLine commandLine = parseCommandLine(argumentCount, arguments);
    if (commandLine.helpRequested) {
        std::cout << USAGE;
        return EXIT_SUCCESS;
    }
    if (!commandLine.error.empty()) {
        std::cerr << "stitching: " << commandLine.error << "\n\n" << USAGE;
        return EXIT_INVALID_USAGE;
    }
    return runPipelineCommand(commandLine);
}

int runFrontend(int argumentCount, char** arguments) {
    QApplication application(argumentCount, arguments); // NOLINT(misc-include-cleaner)
    QApplication::setApplicationName(QStringLiteral("KIK-IRPA Assemblage")); // NOLINT(misc-include-cleaner)
    QApplication::setOrganizationName(QStringLiteral("KIK-IRPA")); // NOLINT(misc-include-cleaner)

    MainWindow window;
    window.show();
    if (std::getenv("KIKIRPA_FRONTEND_STARTUP_TEST") != nullptr)
        QTimer::singleShot(0, &application, &QApplication::quit);
    return QApplication::exec();
}

}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argumentCount, char* arguments[]) {
    try {
        if (isCommandLineMode(argumentCount, arguments)) return runCommandLine(argumentCount, arguments);
        return runFrontend(argumentCount, arguments);
    } catch (const std::exception& exception) {
        std::cerr << "stitching: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
