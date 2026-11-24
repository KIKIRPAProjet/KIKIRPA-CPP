// tests/frontend/test_pipeline_cancel_resume.cpp
// NOLINTBEGIN
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "pipeline_runner.h"

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
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int TILE_SHIFT_X = 320;
constexpr int TEXTURE_SEED = 12345;
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
        fs::create_directories(checkpointsDir);
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
    cv::RNG randomGenerator(TEXTURE_SEED);
    randomGenerator.fill(source, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(source, source, cv::Size(), 2.0);
    cv::normalize(source, source, 2000.0, 60000.0, cv::NORM_MINMAX);
    cv::Mat source16;
    source.convertTo(source16, CV_16U);

    const cv::Rect tileRegions[] = {
        {0, 0, TILE_WIDTH, TILE_HEIGHT},
        {TILE_SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT}};
    const char *tileNames[] = {"img_C00_R00.tif", "img_C01_R00.tif"};
    for (std::size_t index = 0; index < 2; ++index) {
        if (!cv::imwrite((inputDir / tileNames[index]).string(), source16(tileRegions[index])))
            throw std::runtime_error("could not write a synthetic tile");
    }
}

Json makeThreeStepBackendScript(const TempDirGuard &dirs)
{
    Json importStep = PipelineNs::createStep("import")->getConfigJson();
    for (Json &parameter : importStep.at("parameters")) {
        const std::string name = parameter.value("name", "");
        if (name == "path")
            parameter["value"] = dirs.inputDir.string();
        else if (name == "format")
            parameter["value"] = R"(img_C(\d+)_R(\d+)\.tif)";
    }

    const Json steps = Json::array({
        importStep,
        PipelineNs::createStep("phase-correlation")->getConfigJson(),
        PipelineNs::createStep("absolute-pos")->getConfigJson()});
    return {{"pipeline", {{"steps", steps}}},
            {"run", {{"checkpointDirectory", dirs.checkpointsDir.string()}}}};
}

void writeJsonFile(const fs::path &path, const Json &json)
{
    std::ofstream output(path, std::ios::binary);
    output << json.dump(2) << '\n';
    if (!output)
        throw std::runtime_error("could not write the pipeline script");
}

Json readJsonFile(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input);
}

template <typename Runner, typename Signal>
bool waitForSignal(Runner &runner, Signal signal, int timeoutMs = EVENT_LOOP_TIMEOUT_MS)
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

TEST(PipelineCancelResumeTest, PausesAtAStepBoundaryAndResumesFromCheckpoint)
{
    ensureQtApp();
    TempDirGuard dirs;
    createSyntheticTwoTileDataset(dirs.inputDir);
    const Json script = makeThreeStepBackendScript(dirs);

    PipelineRunnerClass runner;
    runner.setScriptJson(QString::fromStdString(script.dump()));
    bool pauseOnStart = true;
    QObject::connect(&runner, &PipelineRunnerClass::runStarted, &runner, [&runner, &pauseOnStart]() {
        if (pauseOnStart) {
            pauseOnStart = false;
            runner.pauseRun();
        }
    });

    runner.startRun();
    ASSERT_TRUE(waitForSignal(runner, &PipelineRunnerClass::pausedStateChanged));
    ASSERT_TRUE(runner.isPaused());

    const fs::path runStatePath = dirs.checkpointsDir / "run.json";
    const Json pausedRun = readJsonFile(runStatePath);
    EXPECT_EQ(pausedRun.at("run").at("status"), "paused");
    EXPECT_LT(pausedRun.at("run").at("completedSteps").size(), 3U);

    runner.resumeRun();
    ASSERT_TRUE(waitForSignal(runner, &PipelineRunnerClass::runFinished));
    EXPECT_FALSE(runner.isPaused());

    const Json finishedRun = readJsonFile(runStatePath);
    EXPECT_EQ(finishedRun.at("run").at("status"), "completed");
    EXPECT_EQ(finishedRun.at("run").at("completedSteps").size(), 3U);
}

TEST(PipelineCancelResumeTest, ResumesPersistedCheckpointWithANewRunner)
{
    ensureQtApp();
    TempDirGuard dirs;
    createSyntheticTwoTileDataset(dirs.inputDir);
    const Json script = makeThreeStepBackendScript(dirs);
    const fs::path scriptPath = dirs.outputDir / "pipeline.json";
    const fs::path runStatePath = dirs.checkpointsDir / "run.json";
    writeJsonFile(scriptPath, script);

    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(scriptPath);
    PipelineNs::RunState state = pipeline.getRunState();
    state.skipCompletedSteps = false;
    pipeline.setRunState(state);
    pipeline.setScriptPath(runStatePath);
    ASSERT_FALSE(pipeline.run([&pipeline](const std::string &message) {
        if (message.rfind("Step 1/3 OK", 0) == 0)
            pipeline.requestPause();
    }));

    const Json pausedRun = readJsonFile(runStatePath);
    ASSERT_EQ(pausedRun.at("run").at("status"), "paused");
    ASSERT_EQ(pausedRun.at("run").at("completedSteps").size(), 1U);

    PipelineRunnerClass runner;
    runner.setScriptJson(QString::fromStdString(script.dump()));
    QStringList resumeLogs;
    QObject::connect(&runner, &PipelineRunnerClass::logMessage,
                     [&resumeLogs](const QString &message) {
                         resumeLogs.append(message);
                     });

    runner.startRun(1, true);
    ASSERT_TRUE(waitForSignal(runner, &PipelineRunnerClass::runFinished));

    EXPECT_TRUE(resumeLogs.contains(QStringLiteral("resuming at step 2/3")));
    const Json finishedRun = readJsonFile(runStatePath);
    EXPECT_EQ(finishedRun.at("run").at("status"), "completed");
    EXPECT_EQ(finishedRun.at("run").at("completedSteps").size(), 3U);
}

} // namespace
// NOLINTEND
