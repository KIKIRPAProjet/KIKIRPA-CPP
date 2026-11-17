// tests/frontend/test_pipeline_worker.cpp
// Tests unitaires de PipelineWorkerClass et PipelineRunnerClass :
// Couvre l'exécution complète, la pause puis la reprise, l'arrêt (annulation),
// et l'intégration asynchrone multi-thread sur deux tuiles générées.
#include "pipeline_worker.h"
#include "pipeline_runner.h"

#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

    // Deux tuiles 16 bits côte à côte, découpées dans une même texture : B est 320 px à droite de A (20 % de recouvrement).
    constexpr int TILE_WIDTH = 400;
    constexpr int TILE_HEIGHT = 300;
    constexpr int TILE_SHIFT = 320;
    constexpr int TEXTURE_SEED = 12345;
    constexpr double TEXTURE_SIGMA = 3.0;
    constexpr double PIXEL_MINIMUM = 2000.0;
    constexpr double PIXEL_MAXIMUM = 60000.0;

    // Deux tuiles <prefix>C00_R00.tif et <prefix>C01_R00.tif avec gradient/texture identique à tests/cli/test_command_line.cpp.
    void writeTiles(const fs::path& directory, const std::string& prefix = "img_") {
        fs::create_directories(directory);
        cv::Mat texture(TILE_HEIGHT, TILE_SHIFT + TILE_WIDTH, CV_32F);
        cv::RNG randomGenerator(TEXTURE_SEED);
        randomGenerator.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(texture, texture, cv::Size(), TEXTURE_SIGMA);
        cv::normalize(texture, texture, PIXEL_MINIMUM, PIXEL_MAXIMUM, cv::NORM_MINMAX);
        cv::Mat texture16;
        texture.convertTo(texture16, CV_16U);
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C00_R00.tif"),
                         texture16(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT)));
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C01_R00.tif"),
                         texture16(cv::Rect(TILE_SHIFT, 0, TILE_WIDTH, TILE_HEIGHT)));
    }

    Json parameter(const std::string& name, const std::string& value) {
        return {{"valueType", "string"}, {"name", name}, {"value", value}};
    }

    // Chaîne complète : import, phase-correlation, absolute-pos, graphcut, fusion, export.
    Json makeScript(const fs::path& tilesDirectory, const fs::path& outputDirectory, const fs::path& checkpointDirectory) {
        const Json steps = Json::array({
            {{"name", "import"},
             {"parameters", Json::array({parameter("path", IONs::pathToUtf8(tilesDirectory)),
                                         parameter("format", "img_C(\\d+)_R(\\d+)\\.tif")})}},
            {{"name", "phase-correlation"}},
            {{"name", "absolute-pos"}},
            {{"name", "graphcut"}},
            {{"name", "fusion"}},
            {{"name", "export"},
             {"parameters", Json::array({parameter("outputDirectory", IONs::pathToUtf8(outputDirectory))})}}
        });
        return {
            {"pipeline", {{"steps", steps}}},
            {"run", {{"checkpointDirectory", IONs::pathToUtf8(checkpointDirectory)}}}
        };
    }

    class PipelineWorkerTestClass : public ::testing::Test {

        protected:
            void SetUp() override {
                const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
                rootDirectory = fs::temp_directory_path() / ("kikirpa-worker-test-" + std::to_string(uniqueValue));
                tilesDirectory = rootDirectory / "tiles";
                outputDirectory = rootDirectory / "output";
                checkpointDirectory = rootDirectory / "checkpoints";

                fs::create_directories(tilesDirectory);
                fs::create_directories(outputDirectory);
                fs::create_directories(checkpointDirectory);

                writeTiles(tilesDirectory);
            }

            void TearDown() override {
                std::error_code errorCode;
                fs::remove_all(rootDirectory, errorCode);
            }

            fs::path rootDirectory;
            fs::path tilesDirectory;
            fs::path outputDirectory;
            fs::path checkpointDirectory;

    };

}

// 1. Exécution complète : started, stepStarted, stepCompleted, progressChanged (100%), pipelineFinished (succès + mosaïque)
TEST_F(PipelineWorkerTestClass, FullRunCompletesSuccessfullyAndGeneratesPreview) {
    const Json script = makeScript(tilesDirectory, outputDirectory, checkpointDirectory);
    const QString scriptText = QString::fromStdString(script.dump());

    PipelineWorkerClass worker;

    bool startedEmitted = false;
    std::vector<int> startedSteps;
    std::vector<int> completedSteps;
    int lastProgress = 0;
    bool finishedEmitted = false;
    bool finishedSuccess = false;
    QString finishedError;
    QImage generatedPreview;

    QObject::connect(&worker, &PipelineWorkerClass::started, [&]() {
        startedEmitted = true;
    });

    QObject::connect(&worker, &PipelineWorkerClass::stepStarted, [&](int stepIndex, int /*total*/, const QString& /*name*/) {
        startedSteps.push_back(stepIndex);
    });

    QObject::connect(&worker, &PipelineWorkerClass::stepCompleted, [&](int stepIndex, int /*total*/, const QString& /*name*/) {
        completedSteps.push_back(stepIndex);
    });

    QObject::connect(&worker, &PipelineWorkerClass::progressChanged, [&](int percentage) {
        lastProgress = percentage;
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool success, const QString& error, const QImage& preview) {
        finishedEmitted = true;
        finishedSuccess = success;
        finishedError = error;
        generatedPreview = preview;
    });

    worker.runPipeline(scriptText, QString(), true, false);

    EXPECT_TRUE(startedEmitted);
    EXPECT_TRUE(finishedEmitted);
    EXPECT_TRUE(finishedSuccess) << finishedError.toStdString();
    EXPECT_TRUE(finishedError.isEmpty());
    EXPECT_EQ(lastProgress, 100);

    // 6 étapes exécutées
    EXPECT_EQ(startedSteps.size(), 6u);
    EXPECT_EQ(completedSteps.size(), 6u);
    if (completedSteps.size() >= 6) {
        EXPECT_EQ(completedSteps.front(), 1);
        EXPECT_EQ(completedSteps.back(), 6);
    }

    // Prévisualisation finale générée (non nulle, dimensions positives)
    EXPECT_FALSE(generatedPreview.isNull());
    EXPECT_GT(generatedPreview.width(), 0);
    EXPECT_GT(generatedPreview.height(), 0);

    // Les fichiers de sortie et de points de reprise sont bien présents
    EXPECT_TRUE(fs::exists(outputDirectory / "mosaic.tif"));
    EXPECT_TRUE(fs::exists(checkpointDirectory / "run.json"));
}

// 2. Pause après la 1ère étape, puis reprise jusqu'à l'achèvement complet
TEST_F(PipelineWorkerTestClass, PauseAfterStepAndResumeToCompletion) {
    const Json script = makeScript(tilesDirectory, outputDirectory, checkpointDirectory);
    const QString scriptText = QString::fromStdString(script.dump());

    // --- Phase 1 : Lancement et demande de pause à l'issue de l'étape 1 ("import") ---
    PipelineWorkerClass worker;

    bool pauseEmitted = false;
    int pausedAtStep = 0;
    QString pausedStepName;
    bool finishedInPhase1 = false;

    QObject::connect(&worker, &PipelineWorkerClass::stepCompleted, [&](int stepIndex, int /*total*/, const QString& /*name*/) {
        if (stepIndex == 1) {
            worker.requestPause();
        }
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelinePaused, [&](int atStep, const QString& name) {
        pauseEmitted = true;
        pausedAtStep = atStep;
        pausedStepName = name;
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool /*success*/, const QString& /*error*/, const QImage& /*preview*/) {
        finishedInPhase1 = true;
    });

    worker.runPipeline(scriptText, QString(), true, false);

    EXPECT_TRUE(pauseEmitted);
    EXPECT_EQ(pausedAtStep, 1);
    EXPECT_EQ(pausedStepName.toStdString(), "import");
    EXPECT_FALSE(finishedInPhase1);
    EXPECT_TRUE(worker.isPaused());

    // run.json doit attester que l'étape 1 est enregistrée
    const fs::path runJsonPath = checkpointDirectory / "run.json";
    ASSERT_TRUE(fs::exists(runJsonPath));
    std::ifstream stateFile(runJsonPath, std::ios::binary);
    const Json stateJson = Json::parse(stateFile);
    EXPECT_EQ(stateJson.at("run").at("status"), "paused");
    EXPECT_EQ(stateJson.at("run").at("completedSteps").size(), 1u);
    EXPECT_EQ(stateJson.at("run").at("completedSteps")[0], "import");

    // mosaic.tif ne doit pas encore exister
    EXPECT_FALSE(fs::exists(outputDirectory / "mosaic.tif"));

    // --- Phase 2 : Reprise (isResume = true, skipCompletedSteps = true) ---
    PipelineWorkerClass resumeWorker;

    bool resumeFinished = false;
    bool resumeSuccess = false;
    QString resumeError;
    std::vector<int> resumedCompletedSteps;

    QObject::connect(&resumeWorker, &PipelineWorkerClass::stepCompleted, [&](int stepIndex, int /*total*/, const QString& /*name*/) {
        resumedCompletedSteps.push_back(stepIndex);
    });

    QObject::connect(&resumeWorker, &PipelineWorkerClass::pipelineFinished, [&](bool success, const QString& error, const QImage& /*preview*/) {
        resumeFinished = true;
        resumeSuccess = success;
        resumeError = error;
    });

    resumeWorker.runPipeline(scriptText, QString(), true, /*isResume=*/true);

    EXPECT_TRUE(resumeFinished);
    EXPECT_TRUE(resumeSuccess) << resumeError.toStdString();
    EXPECT_TRUE(resumeError.isEmpty());

    // L'étape 1 ayant été sautée, les étapes terminées lors de la reprise vont de 2 à 6 (5 étapes restantes)
    EXPECT_EQ(resumedCompletedSteps.size(), 5u);
    if (!resumedCompletedSteps.empty()) {
        EXPECT_EQ(resumedCompletedSteps.front(), 2);
        EXPECT_EQ(resumedCompletedSteps.back(), 6);
    }

    // Le résultat final est maintenant bien produit
    EXPECT_TRUE(fs::exists(outputDirectory / "mosaic.tif"));
}

// 3. Arrêt (annulation) : émission de pipelineCancelled et absence de pipelineFinished
TEST_F(PipelineWorkerTestClass, CancelStopsPipelineCleanly) {
    const Json script = makeScript(tilesDirectory, outputDirectory, checkpointDirectory);
    const QString scriptText = QString::fromStdString(script.dump());

    PipelineWorkerClass worker;

    bool cancelEmitted = false;
    bool finishedEmitted = false;
    bool pausedEmitted = false;

    QObject::connect(&worker, &PipelineWorkerClass::stepCompleted, [&](int stepIndex, int /*total*/, const QString& /*name*/) {
        if (stepIndex == 1) {
            worker.requestCancel();
        }
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelineCancelled, [&]() {
        cancelEmitted = true;
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool /*success*/, const QString& /*error*/, const QImage& /*preview*/) {
        finishedEmitted = true;
    });

    QObject::connect(&worker, &PipelineWorkerClass::pipelinePaused, [&](int /*atStep*/, const QString& /*name*/) {
        pausedEmitted = true;
    });

    worker.runPipeline(scriptText, QString(), true, false);

    EXPECT_TRUE(cancelEmitted);
    EXPECT_TRUE(worker.isCancelled());
    EXPECT_FALSE(finishedEmitted);
    EXPECT_FALSE(pausedEmitted);

    // Les étapes avales n'ont pas été exécutées
    EXPECT_FALSE(fs::exists(outputDirectory / "mosaic.tif"));
}

// 4. Erreur de script ou de données : pipelineFinished avec success=false
TEST_F(PipelineWorkerTestClass, ReportsErrorWhenScriptIsInvalid) {
    PipelineWorkerClass worker;

    bool finishedEmitted = false;
    bool finishedSuccess = true;
    QString errorMessage;

    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool success, const QString& error, const QImage& /*preview*/) {
        finishedEmitted = true;
        finishedSuccess = success;
        errorMessage = error;
    });

    // Script JSON non conforme (pas d'étapes)
    const QString invalidJson = QStringLiteral("{ \"invalid\": true }");
    worker.runPipeline(invalidJson, QString(), true, false);

    EXPECT_TRUE(finishedEmitted);
    EXPECT_FALSE(finishedSuccess);
    EXPECT_FALSE(errorMessage.isEmpty());
}

// 5. Test asynchrone multi-thread via PipelineRunnerClass
TEST_F(PipelineWorkerTestClass, AsyncRunnerExecutesInDedicatedThread) {
    const Json script = makeScript(tilesDirectory, outputDirectory, checkpointDirectory);
    const QString scriptText = QString::fromStdString(script.dump());

    PipelineRunnerClass runner;
    runner.setScriptJson(scriptText);

    bool runStartedEmitted = false;
    bool runFinishedEmitted = false;
    bool mosaicReadyEmitted = false;
    int finalProgress = 0;

    QEventLoop loop;

    QObject::connect(&runner, &PipelineRunnerClass::runStarted, [&]() {
        runStartedEmitted = true;
    });

    QObject::connect(&runner, &PipelineRunnerClass::progressAdvanced, [&](int percentage) {
        finalProgress = percentage;
    });

    QObject::connect(&runner, &PipelineRunnerClass::mosaicReady, [&](const QImage& img) {
        if (!img.isNull()) {
            mosaicReadyEmitted = true;
        }
    });

    QObject::connect(&runner, &PipelineRunnerClass::runFinished, [&]() {
        runFinishedEmitted = true;
        loop.quit();
    });

    // Timeout de sécurité au cas où l'événement ne surviendrait pas
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);

    runner.startRun();
    EXPECT_TRUE(runner.isRunning());

    loop.exec();

    EXPECT_TRUE(runStartedEmitted);
    EXPECT_TRUE(runFinishedEmitted);
    EXPECT_TRUE(mosaicReadyEmitted);
    EXPECT_EQ(finalProgress, 100);
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(fs::exists(outputDirectory / "mosaic.tif"));
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
