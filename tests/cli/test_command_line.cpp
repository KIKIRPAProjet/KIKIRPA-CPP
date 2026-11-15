// tests/cli/test_command_line.cpp
// Lance l'exécutable `stitching` comme un utilisateur (std::system) et vérifie codes de sortie, messages et reprise.
// Les tuiles sont générées par le test : aucun fichier externe.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

    constexpr int EXIT_STEP_FAILED = 1;
    constexpr int EXIT_INVALID_USAGE = 2;
    constexpr int EXIT_IO_ERROR = 3;
    // Deux tuiles 16 bits côte à côte, découpées dans une même texture : B est 320 px à droite de A (20 % de recouvrement).
    constexpr int TILE_WIDTH = 400;
    constexpr int TILE_HEIGHT = 300;
    constexpr int TILE_SHIFT = 320;
    constexpr int TEXTURE_SEED = 12345;
    constexpr double TEXTURE_SIGMA = 3.0;
    constexpr double PIXEL_MINIMUM = 2000.0;
    constexpr double PIXEL_MAXIMUM = 60000.0;

    struct CommandResult {
        int exitCode = -1;
        std::string output;   // stdout
        std::string errors;   // stderr
    };

    std::string readText(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    int toExitCode(int systemStatus) {
#ifdef _WIN32
        return systemStatus;
#else
        return WIFEXITED(systemStatus) ? WEXITSTATUS(systemStatus) : -1;
#endif
    }

    // Lance `stitching <arguments>` depuis le dossier courant du test ; stdout et stderr passent par des fichiers.
    CommandResult runStitching(const std::string& arguments, const fs::path& logDirectory) {
        const fs::path outputFile = logDirectory / "stdout.txt";
        const fs::path errorFile = logDirectory / "stderr.txt";
        std::string command = "\"" STITCHING_EXECUTABLE "\" " + arguments + " > \"" + outputFile.string() + "\" 2> \"" +
                              errorFile.string() + "\"";
#ifdef _WIN32
        command = "\"" + command + "\"";   // cmd /c retire la première et la dernière guillemets de la ligne
#endif
        CommandResult result;
        // NOLINTNEXTLINE(bugprone-command-processor) : le test lance l'exécutable comme un utilisateur, ligne écrite ici.
        result.exitCode = toExitCode(std::system(command.c_str()));
        result.output = readText(outputFile);
        result.errors = readText(errorFile);
        return result;
    }

    std::string quoted(const fs::path& path) { return "\"" + path.string() + "\""; }

    void writeTiles(const fs::path& directory) {
        fs::create_directories(directory);
        cv::Mat texture(TILE_HEIGHT, TILE_SHIFT + TILE_WIDTH, CV_32F);
        cv::RNG randomGenerator(TEXTURE_SEED);
        randomGenerator.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(texture, texture, cv::Size(), TEXTURE_SIGMA);
        cv::normalize(texture, texture, PIXEL_MINIMUM, PIXEL_MAXIMUM, cv::NORM_MINMAX);
        cv::Mat texture16;
        texture.convertTo(texture16, CV_16U);
        cv::imwrite((directory / "img_C00_R00.tif").string(), texture16(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT)));
        cv::imwrite((directory / "img_C01_R00.tif").string(), texture16(cv::Rect(TILE_SHIFT, 0, TILE_WIDTH, TILE_HEIGHT)));
    }

    // Script import → phase-correlation ; `imagePath` tel qu'écrit dans le script (relatif ou non).
    Json makeScript(const std::string& imagePath) {
        return {{"pipeline", {{"steps", Json::array({
                    {{"name", "import"},
                     {"parameters", Json::array({{{"valueType", "string"}, {"name", "path"}, {"value", imagePath}}})}},
                    {{"name", "phase-correlation"}}})}}}};
    }

    // Le même script, avec des checkpoints (et donc la reprise) dans `checkpointDirectory`.
    Json withCheckpoints(Json script, const std::string& checkpointDirectory) {
        script["run"] = {{"checkpointDirectory", checkpointDirectory}};
        return script;
    }

    void writeJson(const fs::path& path, const Json& json) { std::ofstream(path, std::ios::binary) << json.dump(2); }

    // Dossier propre à chaque test : <temp>/kikirpa-cli-test-<horloge>/, avec les tuiles dans tiles/.
    class CommandLineTestClass : public ::testing::Test {

        protected:
            void SetUp() override {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                directory = fs::temp_directory_path() / ("kikirpa-cli-test-" + std::to_string(uniqueValue));
                fs::create_directories(directory);
                writeTiles(directory / "tiles");
            }

            void TearDown() override {
                std::error_code errorCode;
                fs::remove_all(directory, errorCode);
            }

            fs::path directory;

    };

}

TEST_F(CommandLineTestClass, HelpPrintsTheUsageAndSucceeds) {
    const CommandResult result = runStitching("--help", directory);

    EXPECT_EQ(result.exitCode, EXIT_SUCCESS);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

// Une option inconnue ne doit pas ouvrir l'interface graphique : usage sur stderr, code 2.
TEST_F(CommandLineTestClass, UnknownOptionIsRejected) {
    const CommandResult result = runStitching("--pipline script.json", directory);

    EXPECT_EQ(result.exitCode, EXIT_INVALID_USAGE);
    EXPECT_NE(result.errors.find("unknown option '--pipline'"), std::string::npos);
}

TEST_F(CommandLineTestClass, PipelineOptionNeedsAScript) {
    EXPECT_EQ(runStitching("--pipeline", directory).exitCode, EXIT_INVALID_USAGE);
    EXPECT_EQ(runStitching("--resume", directory).exitCode, EXIT_INVALID_USAGE);
}

TEST_F(CommandLineTestClass, MissingScriptIsAFileError) {
    const CommandResult result = runStitching("--pipeline " + quoted(directory / "absent.json"), directory);

    EXPECT_EQ(result.exitCode, EXIT_IO_ERROR);
    EXPECT_NE(result.errors.find("Pipeline error: cannot open"), std::string::npos);
}

TEST_F(CommandLineTestClass, InvalidScriptRunsNothing) {
    Json script = makeScript("tiles");
    script["pipeline"]["steps"][1]["name"] = "phase-corelation";
    writeJson(directory / "script.json", script);

    const CommandResult result = runStitching("--pipeline " + quoted(directory / "script.json"), directory);

    EXPECT_EQ(result.exitCode, EXIT_INVALID_USAGE);
    EXPECT_NE(result.errors.find("unknown step 'phase-corelation'"), std::string::npos);
    EXPECT_EQ(result.output.find("Step 1/2"), std::string::npos);
}

// Les chemins relatifs du script se lisent depuis le dossier du script, quel que soit le dossier courant du test.
TEST_F(CommandLineTestClass, RunsAScriptWithPathsRelativeToIt) {
    writeJson(directory / "script.json", makeScript("tiles"));

    const CommandResult result = runStitching("--pipeline " + quoted(directory / "script.json"), directory);

    EXPECT_EQ(result.exitCode, EXIT_SUCCESS) << result.output << result.errors;
    EXPECT_NE(result.output.find("Step 2/2 OK"), std::string::npos);
}

TEST_F(CommandLineTestClass, FailedStepIsNamedOnStandardError) {
    writeJson(directory / "script.json", withCheckpoints(makeScript("absent"), "checkpoints"));

    const CommandResult result = runStitching("--pipeline " + quoted(directory / "script.json"), directory);

    EXPECT_EQ(result.exitCode, EXIT_STEP_FAILED);
    EXPECT_NE(result.errors.find("step 1/2 'import' failed"), std::string::npos) << result.errors;
    EXPECT_NE(result.errors.find("--resume"), std::string::npos);
}

// Reprise : l'état est écrit dans <checkpoints>/run.json ; --resume saute les steps faits, sans --resume tout est refait.
TEST_F(CommandLineTestClass, ResumeSkipsTheCompletedSteps) {
    writeJson(directory / "script.json", withCheckpoints(makeScript("tiles"), "checkpoints"));
    const std::string runScript = "--pipeline " + quoted(directory / "script.json");
    ASSERT_EQ(runStitching(runScript, directory).exitCode, EXIT_SUCCESS);
    ASSERT_TRUE(fs::exists(directory / "checkpoints" / "run.json"));

    const CommandResult resumed = runStitching(runScript + " --resume", directory);
    EXPECT_EQ(resumed.exitCode, EXIT_SUCCESS);
    EXPECT_NE(resumed.output.find("nothing to do"), std::string::npos) << resumed.output;

    const CommandResult restarted = runStitching(runScript, directory);
    EXPECT_EQ(restarted.exitCode, EXIT_SUCCESS);
    EXPECT_NE(restarted.output.find("Step 1/2 OK"), std::string::npos) << restarted.output;
}

TEST_F(CommandLineTestClass, ResumeWithoutPreviousRunStartsFromTheFirstStep) {
    writeJson(directory / "script.json", withCheckpoints(makeScript("tiles"), "checkpoints"));

    const CommandResult result = runStitching("--pipeline " + quoted(directory / "script.json") + " --resume", directory);

    EXPECT_EQ(result.exitCode, EXIT_SUCCESS);
    EXPECT_NE(result.output.find("no previous run"), std::string::npos);
    EXPECT_NE(result.output.find("Step 1/2 OK"), std::string::npos);
}

TEST_F(CommandLineTestClass, ResumeNeedsACheckpointDirectory) {
    writeJson(directory / "script.json", makeScript("tiles"));

    const CommandResult result = runStitching("--pipeline " + quoted(directory / "script.json") + " --resume", directory);

    EXPECT_EQ(result.exitCode, EXIT_INVALID_USAGE);
    EXPECT_NE(result.errors.find("checkpointDirectory"), std::string::npos);
}

TEST_F(CommandLineTestClass, EmptyPipelineSucceeds) {
    writeJson(directory / "script.json", {{"pipeline", {{"steps", Json::array()}}}});

    EXPECT_EQ(runStitching("--pipeline " + quoted(directory / "script.json"), directory).exitCode, EXIT_SUCCESS);
}
