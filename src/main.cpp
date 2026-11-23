// src/main.cpp
#include "core/io/unicode_path.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "mainwindow.h"

#include <QtCore/QString>
#include <QtCore/qtimer.h>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

    namespace fs = std::filesystem;
    using Json = nlohmann::json;

    // Codes de sortie de `stitching --pipeline`, repris dans le texte d'aide et dans docs/core/Pipeline.md.
    constexpr int EXIT_STEP_FAILED = 1;     // un step a échoué : le détail est sur stderr
    constexpr int EXIT_INVALID_USAGE = 2;   // ligne de commande ou script invalide : rien n'a été exécuté
    constexpr int EXIT_IO_ERROR = 3;        // script ou état de reprise illisible, dossier inaccessible

    constexpr const char* USAGE =
        "Usage:\n"
        "  stitching                                    open the graphical interface\n"
        "  stitching --pipeline <script.json> [--resume] run a pipeline script\n"
        "  stitching --help                             show this help\n"
        "\n"
        "Relative paths in the script are read from the script's directory.\n"
        "--resume continues the previous run of the script from its last completed step; it needs\n"
        "run.checkpointDirectory in the script. Without --resume, every step is run again.\n"
        "\n"
        "Exit codes: 0 success, 1 a step failed, 2 invalid command line or script, 3 file error.\n";

    // Ligne de commande du mode pipeline.
    struct CommandLine {
        bool isHelpRequested = false;
        bool isResumeRequested = false;
        std::string scriptPath;
        std::string error;   // non vide : ligne de commande invalide
    };

    // Erreur de fichier (script, état de reprise, dossier) : code EXIT_IO_ERROR.
    class FileErrorClass : public std::runtime_error {

        public:
            using std::runtime_error::runtime_error;

    };

    // Pendant le mode pipeline, la console Windows affiche l'UTF-8 (chemins des messages compris) ; sa page de code
    // d'origine lui est rendue à la fin, parce qu'elle appartient au terminal de l'utilisateur. Sans effet ailleurs.
    class Utf8ConsoleClass {

        public:
            Utf8ConsoleClass() {
#ifdef _WIN32
                previousCodePage = GetConsoleOutputCP();
                SetConsoleOutputCP(CP_UTF8);
#endif
            }

            ~Utf8ConsoleClass() {
#ifdef _WIN32
                if (previousCodePage != 0) SetConsoleOutputCP(previousCodePage);
#endif
            }

            Utf8ConsoleClass(const Utf8ConsoleClass&) = delete;
            Utf8ConsoleClass& operator=(const Utf8ConsoleClass&) = delete;

        private:
            unsigned int previousCodePage = 0;

    };

    // Arguments en UTF-8. Sous Windows, argv est dans la page de code du système, qui perd les caractères qu'elle n'a
    // pas (« 作品 » devient « ?? ») : la ligne de commande est relue en UTF-16 puis convertie.
    std::vector<std::string> utf8Arguments(int argumentCount, char** arguments) {
#ifdef _WIN32
        int wideArgumentCount = 0;
        LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
        if (wideArguments != nullptr) {
            std::vector<std::string> converted;
            converted.reserve(static_cast<std::size_t>(wideArgumentCount));
            for (int argumentIndex = 0; argumentIndex < wideArgumentCount; ++argumentIndex)
                converted.push_back(IONs::pathToUtf8(fs::path(wideArguments[argumentIndex])));
            LocalFree(static_cast<HLOCAL>(wideArguments));
            return converted;
        }
#endif
        return {arguments, arguments + argumentCount};
    }

    // Le mode pipeline commence par une option longue (--pipeline, --resume, --help) ou par -h. Les autres
    // arguments (options de Qt comme -platform, ou rien) ouvrent l'interface graphique.
    bool isCommandLineMode(int argumentCount, char** arguments) {
        if (argumentCount < 2) return false;
        const std::string firstArgument = arguments[1];
        return firstArgument.rfind("--", 0) == 0 || firstArgument == "-h";
    }

    CommandLine parseCommandLine(const std::vector<std::string>& arguments) {
        CommandLine commandLine;
        for (std::size_t argumentIndex = 1; argumentIndex < arguments.size() && commandLine.error.empty(); ++argumentIndex) {
            const std::string& argument = arguments[argumentIndex];
            if (argument == "--help" || argument == "-h") {
                commandLine.isHelpRequested = true;
            } else if (argument == "--resume") {
                commandLine.isResumeRequested = true;
            } else if (argument == "--pipeline") {
                const bool hasScript = argumentIndex + 1 < arguments.size() && !arguments[argumentIndex + 1].empty();
                if (hasScript) commandLine.scriptPath = arguments[++argumentIndex];
                else commandLine.error = "--pipeline needs a script file";
            } else {
                commandLine.error = "unknown option '" + argument + "'";
            }
        }
        if (commandLine.error.empty() && !commandLine.isHelpRequested && commandLine.scriptPath.empty())
            commandLine.error = "missing --pipeline <script.json>";
        return commandLine;
    }

    // Le script est désigné par rapport au dossier courant ; le dossier courant devient ensuite celui du script, pour
    // que les chemins relatifs du script (images, checkpoints, sorties) ne dépendent pas de l'endroit d'où on lance.
    // `scriptArgument` est en UTF-8 (utf8Arguments).
    fs::path enterScriptDirectory(const std::string& scriptArgument) {
        std::error_code errorCode;
        const fs::path scriptFile = fs::absolute(IONs::pathFromUtf8(scriptArgument), errorCode);
        if (errorCode || !std::ifstream(scriptFile).good()) throw FileErrorClass("cannot open " + scriptArgument);
        fs::current_path(scriptFile.parent_path(), errorCode);
        if (errorCode)
            throw FileErrorClass("cannot enter the script directory " + IONs::pathToUtf8(scriptFile.parent_path()) +
                                 ": " + errorCode.message());
        return scriptFile;
    }

    // Fichier où le pipeline réécrit le script complet (pipeline + run) après chaque step : <checkpoints>/run.json.
    fs::path runStatePath(const PipelineNs::RunState& runState) {
        return fs::absolute(IONs::pathFromUtf8(runState.checkpointDirectory)) / "run.json";
    }

    Json readJsonFile(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw FileErrorClass("cannot open " + IONs::pathToUtf8(path));
        try {
            return Json::parse(input);
        } catch (const Json::parse_error& exception) {
            throw FileErrorClass("invalid JSON in " + IONs::pathToUtf8(path) + ": " + exception.what());
        }
    }

    // Steps terminés du run précédent, lus dans <checkpoints>/run.json ; aucun si ce fichier n'existe pas encore.
    std::vector<std::string> previousCompletedSteps(const PipelineNs::RunState& runState) {
        if (runState.checkpointDirectory.empty())
            throw std::invalid_argument("--resume needs run.checkpointDirectory in the script");
        const fs::path statePath = runStatePath(runState);
        if (!fs::exists(statePath)) {
            std::cout << "no previous run in " << IONs::pathToUtf8(statePath) << ": starting from the first step\n";
            return {};
        }
        const Json stateJson = readJsonFile(statePath);
        if (!stateJson.contains("run")) return {};
        return PipelineNs::RunState::fromJson(stateJson.at("run")).completedSteps;
    }

    // Sans --resume, tous les steps sont refaits. Avec --resume, l'état du run précédent (<checkpoints>/run.json)
    // est repris : le pipeline saute les steps terminés dont le checkpoint est encore valide (paramètres et entrées
    // inchangés) et reprend au premier step à refaire. L'état de ce run est à son tour écrit dans run.json.
    void prepareRunState(PipelineNs::PipelineClass& pipeline, bool isResumeRequested) {
        PipelineNs::RunState runState = pipeline.getRunState();
        runState.status = PipelineNs::RunStatus::Idle;
        runState.completedSteps = isResumeRequested ? previousCompletedSteps(runState) : std::vector<std::string>{};
        runState.skipCompletedSteps = true;
        pipeline.setRunState(runState);
        if (!runState.checkpointDirectory.empty()) pipeline.setScriptPath(runStatePath(runState));
    }

    // Après un échec, nomme le step fautif sur stderr (la progression, elle, est sur stdout).
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

    // L'exception d'un step est déjà transformée en échec par PipelineClass::run. Une exception qui en sort quand même
    // (checkpoint illisible, disque plein) arrête le run : c'est un échec d'exécution, pas une erreur de script, sauf
    // std::invalid_argument (run.startFromStep invalide), laissée à l'appelant.
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
        PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(enterScriptDirectory(commandLine.scriptPath));
        prepareRunState(pipeline, commandLine.isResumeRequested);
        if (executePipeline(pipeline)) return EXIT_SUCCESS;
        reportFailedStep(pipeline, commandLine.scriptPath);
        return EXIT_STEP_FAILED;
    }

    // Une erreur avant l'exécution (fichier, script invalide) n'exécute aucun step.
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
        const Utf8ConsoleClass utf8Console;
        const CommandLine commandLine = parseCommandLine(utf8Arguments(argumentCount, arguments));
        if (commandLine.isHelpRequested) {
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
        // Icône de toutes les fenêtres (barre de titre, barre des tâches, Alt+Tab) ; resources/resources.qrc.
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo/logo.png"))); // NOLINT(misc-include-cleaner)

        MainWindow window;
        window.show();
        // Test de démarrage (tests/cli) : la fenêtre s'ouvre puis l'application se ferme aussitôt.
        if (std::getenv("KIKIRPA_FRONTEND_STARTUP_TEST") != nullptr) QTimer::singleShot(0, &application, &QApplication::quit);
        return QApplication::exec();
    }

}

// NOLINTNEXTLINE(bugprone-exception-escape) : seul std::cerr, dans le catch, peut encore lever.
int main(int argumentCount, char* arguments[]) {
    try {
        if (isCommandLineMode(argumentCount, arguments)) return runCommandLine(argumentCount, arguments);
        return runFrontend(argumentCount, arguments);
    } catch (const std::exception& exception) {   // filet de sécurité : rien ne doit sortir de main sans message
        std::cerr << "stitching: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
