// tests/frontend/test_pipeline_cancel_resume.cpp
// NOLINTBEGIN
#include "project_setup_model.h"
#include "simulationrunner.h"

#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int TILE_SHIFT_X = 320;
constexpr int TEXTURE_SEED = 12345;
constexpr double TEXTURE_SIGMA = 2.0;
constexpr double PIXEL_MIN = 2000.0;
constexpr double PIXEL_MAX = 60000.0;
constexpr int EVENT_LOOP_TIMEOUT_MS = 30000;

int dummyArgc = 1;
char dummyArg0[] = "test_frontend_pipeline_cancel_resume";
char *dummyArgv[] = {dummyArg0, nullptr};

QCoreApplication *ensureQtApp()
{
    if (!QCoreApplication::instance()) {
        static QCoreApplication app(dummyArgc, dummyArgv);
    }
    return QCoreApplication::instance();
}

struct TempDirGuard {
    TempDirGuard()
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("kikirpa_cancel_resume_" + std::to_string(tick));
        inputDir = root / "input";
        outputDir = root / "output";
        checkpointsDir = outputDir / "checkpoints";
        fs::create_directories(inputDir);
        fs::create_directories(outputDir);
    }

    ~TempDirGuard()
    {
        std::error_code errorCode;
        fs::remove_all(root, errorCode);
    }

    fs::path root;
    fs::path inputDir;
    fs::path outputDir;
    fs::path checkpointsDir;
};

void createSyntheticTwoTileDataset(const fs::path &inputDir)
{
    cv::Mat source(TILE_HEIGHT, TILE_SHIFT_X + TILE_WIDTH, CV_32F);
    cv::RNG rng(TEXTURE_SEED);
    rng.fill(source, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(source, source, cv::Size(), TEXTURE_SIGMA);
    cv::normalize(source, source, PIXEL_MIN, PIXEL_MAX, cv::NORM_MINMAX);
    cv::Mat source16;
    source.convertTo(source16, CV_16U);
    cv::imwrite((inputDir / "img_C00_R00.tif").string(), source16(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT)));
    cv::imwrite((inputDir / "img_C01_R00.tif").string(), source16(cv::Rect(TILE_SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT)));
}

fs::path writeThreeStepBackendScript(const TempDirGuard &dirs)
{
    Json importStep = PipelineNs::createStep("import")->getConfigJson();
    for (Json &param : importStep.at("parameters")) {
        if (param.value("name", "") == "path")
            param["value"] = dirs.inputDir.string();
        else if (param.value("name", "") == "format")
            param["value"] = R"(img_C(\d+)_R(\d+)\.tif)";
    }

    Json phaseStep = PipelineNs::createStep("phase-correlation")->getConfigJson();
    Json absStep = PipelineNs::createStep("absolute-pos")->getConfigJson();

    const Json script = {
        {"pipeline", {{"steps", Json::array({importStep, phaseStep, absStep})}}},
        {"run",
         {{"status", "idle"},
          {"completedSteps", Json::array()},
          {"skipCompletedSteps", true},
          {"startFromStep", ""},
          {"checkpointDirectory", dirs.checkpointsDir.string()}}}};

    const fs::path scriptPath = dirs.outputDir / "pipeline.json";
    std::ofstream out(scriptPath, std::ios::binary);
    out << script.dump(2) << '\n';
    return scriptPath;
}

Json readJsonFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return Json::parse(in);
}

template <typename Signal>
bool waitForSignal(SimulationRunner &runner, Signal signal, int timeoutMs = EVENT_LOOP_TIMEOUT_MS)
{
    QEventLoop loop;
    bool triggered = false;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&runner, signal, &loop, [&loop, &triggered]() {
        triggered = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(timeoutMs);
    loop.exec();
    return triggered;
}

TEST(PipelineCancelResumeTest, CancelsRunningBackendPipelineAndResumesFromScript)
{
    ensureQtApp();
    TempDirGuard dirs;
    createSyntheticTwoTileDataset(dirs.inputDir);
    const fs::path scriptPath = writeThreeStepBackendScript(dirs);
    const QString qtScriptPath = QString::fromStdString(scriptPath.string());

    SimulationRunner runner;
    QStringList logs;
    bool cancellationRequestedSignalSeen = false;
    bool pauseSignalSeen = false;

    QObject::connect(&runner, &SimulationRunner::backendLogReceived,
                     [&logs](const QString &message, const QString &) {
                         logs.append(message);
                     });
    QObject::connect(&runner, &SimulationRunner::cancellationRequested,
                     [&cancellationRequestedSignalSeen]() {
                         cancellationRequestedSignalSeen = true;
                     });
    QObject::connect(&runner, &SimulationRunner::runPaused,
                     [&pauseSignalSeen]() {
                         pauseSignalSeen = true;
                     });

    runner.startBackendRun(qtScriptPath, 1);
    EXPECT_TRUE(runner.isRunning());
    runner.cancelRun();
    EXPECT_TRUE(cancellationRequestedSignalSeen);
    EXPECT_TRUE(runner.isCancellationPending());

    ASSERT_TRUE(waitForSignal(runner, &SimulationRunner::runPaused));
    EXPECT_TRUE(pauseSignalSeen);
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(runner.isPaused());
    EXPECT_TRUE(runner.canResume());

    const Json pausedScript = readJsonFile(scriptPath);
    EXPECT_EQ(pausedScript.at("run").at("status"), "paused");
    const std::size_t completedBeforeResume = pausedScript.at("run").at("completedSteps").size();
    EXPECT_LT(completedBeforeResume, 3U);

    logs.clear();
    runner.resumeRun();
    EXPECT_TRUE(runner.isRunning());
    ASSERT_TRUE(waitForSignal(runner, &SimulationRunner::runFinished));

    EXPECT_FALSE(runner.isRunning());
    EXPECT_FALSE(runner.isPaused());
    EXPECT_FALSE(runner.canResume());
    EXPECT_EQ(runner.currentProgress(), 100);

    const Json finishedScript = readJsonFile(scriptPath);
    EXPECT_EQ(finishedScript.at("run").at("status"), "completed");
    EXPECT_EQ(finishedScript.at("run").at("completedSteps").size(), 3U);
}

TEST(PipelineCancelResumeTest, DetectsPausedScriptAndSkipsCompletedStepsOnResume)
{
    ensureQtApp();
    TempDirGuard dirs;
    createSyntheticTwoTileDataset(dirs.inputDir);
    const fs::path scriptPath = writeThreeStepBackendScript(dirs);

    // Prépare un script déjà arrêté en pause après la première étape (import) via le backend.
    {
        PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(scriptPath);
        PipelineNs::RunState state = pipeline.getRunState();
        state.skipCompletedSteps = false;
        pipeline.setRunState(state);
        pipeline.setScriptPath(scriptPath);
        ASSERT_FALSE(pipeline.run([&pipeline](const std::string &message) {
            if (message.rfind("Step 1/3 OK", 0) == 0)
                pipeline.requestPause();
        }));
    }

    const Json pausedScript = readJsonFile(scriptPath);
    ASSERT_EQ(pausedScript.at("run").at("status"), "paused");
    ASSERT_EQ(pausedScript.at("run").at("completedSteps").size(), 1U);

    SimulationRunner runner;
    QStringList resumeLogs;
    QObject::connect(&runner, &SimulationRunner::backendLogReceived,
                     [&resumeLogs](const QString &message, const QString &) {
                         resumeLogs.append(message);
                     });

    runner.setBackendScriptPath(QString::fromStdString(scriptPath.string()));
    EXPECT_TRUE(runner.isPaused());
    EXPECT_TRUE(runner.canResume());
    EXPECT_EQ(runner.currentProgress(), 33);

    runner.resumeRun();
    ASSERT_TRUE(waitForSignal(runner, &SimulationRunner::runFinished));

    EXPECT_FALSE(runner.canResume());
    EXPECT_TRUE(resumeLogs.contains(QStringLiteral("resuming at step 2/3")));
    const Json completedScript = readJsonFile(scriptPath);
    EXPECT_EQ(completedScript.at("run").at("status"), "completed");
    EXPECT_EQ(completedScript.at("run").at("completedSteps").size(), 3U);
}

TEST(PipelineCancelResumeTest, PausesAndResumesDemoSimulationWithoutLosingProgress)
{
    ensureQtApp();
    SimulationRunner runner;
    runner.startRun(1);
    ASSERT_TRUE(runner.isRunning());

    QEventLoop waitProgressLoop;
    QObject::connect(&runner, &SimulationRunner::progressAdvanced,
                     [&waitProgressLoop](int percentage) {
                         if (percentage >= 12)
                             waitProgressLoop.quit();
                     });
    QTimer::singleShot(5000, &waitProgressLoop, &QEventLoop::quit);
    waitProgressLoop.exec();

    const int progressBeforePause = runner.currentProgress();
    ASSERT_GE(progressBeforePause, 12);

    runner.cancelRun();
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(runner.isPaused());
    EXPECT_TRUE(runner.canResume());
    EXPECT_EQ(runner.currentProgress(), progressBeforePause);

    runner.resumeRun();
    EXPECT_TRUE(runner.isRunning());
    EXPECT_FALSE(runner.canResume());
    ASSERT_TRUE(waitForSignal(runner, &SimulationRunner::runFinished));
    EXPECT_EQ(runner.currentProgress(), 100);
}

} // namespace
// NOLINTEND
