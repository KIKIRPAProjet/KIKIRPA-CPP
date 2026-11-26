// tests/frontend/test_pipeline_worker.cpp
// Tests unitaires de PipelineWorkerClass et PipelineRunnerClass :
// Couvre l'exécution complète, la pause puis la reprise, l'arrêt (annulation),
// et l'intégration asynchrone multi-thread sur deux tuiles générées.
#include "pipeline_worker.h"
#include "pipeline_runner.h"
#include "project_serializer.h"

#include "core/io/pyramid_cache.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner)
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QtCore/QEventLoop>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/qglobal.h>
#include <QtGui/QImage>
#include <QtWidgets/QApplication>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json; // NOLINT(misc-include-cleaner)

namespace {

    // Deux tuiles 16 bits côte à côte, découpées dans une même texture : B est 320 px à droite de A (20 % de recouvrement).
    constexpr int TILE_WIDTH = 400;
    constexpr int TILE_HEIGHT = 300;
    constexpr int TILE_SHIFT = 320;
    constexpr int TEXTURE_SEED = 12345;
    constexpr double TEXTURE_SIGMA = 3.0;
    constexpr double PIXEL_MINIMUM = 2000.0;
    constexpr double PIXEL_MAXIMUM = 60000.0;

    constexpr std::size_t TOTAL_PIPELINE_STEPS = 6;
    constexpr std::size_t RESUMED_COMPLETED_STEPS_COUNT = 5;
    constexpr int FULL_PROGRESS_PERCENT = 100;
    constexpr int ASYNC_WAIT_TIMEOUT_MS = 15000;
    constexpr int LAST_STEP_INDEX = 6;

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

    // 4 tuiles 16 bits en grille 2x2 (20 % de recouvrement horizontal et vertical).
    constexpr int TILE_SHIFT_Y = 240;

    void writeTiles2x2(const fs::path& directory, const std::string& prefix = "img_") {
        fs::create_directories(directory);
        const int fullWidth = TILE_SHIFT + TILE_WIDTH;
        const int fullHeight = TILE_SHIFT_Y + TILE_HEIGHT;
        cv::Mat texture(fullHeight, fullWidth, CV_32F);
        cv::RNG randomGenerator(TEXTURE_SEED);
        randomGenerator.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(texture, texture, cv::Size(), TEXTURE_SIGMA);
        cv::normalize(texture, texture, PIXEL_MINIMUM, PIXEL_MAXIMUM, cv::NORM_MINMAX);
        cv::Mat texture16;
        texture.convertTo(texture16, CV_16U);

        // C00_R00 (en haut à gauche)
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C00_R00.tif"),
                         texture16(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT)));
        // C01_R00 (en haut à droite)
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C01_R00.tif"),
                         texture16(cv::Rect(TILE_SHIFT, 0, TILE_WIDTH, TILE_HEIGHT)));
        // C00_R01 (en bas à gauche)
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C00_R01.tif"),
                         texture16(cv::Rect(0, TILE_SHIFT_Y, TILE_WIDTH, TILE_HEIGHT)));
        // C01_R01 (en bas à droite)
        IONs::writeImage(directory / IONs::pathFromUtf8(prefix + "C01_R01.tif"),
                         texture16(cv::Rect(TILE_SHIFT, TILE_SHIFT_Y, TILE_WIDTH, TILE_HEIGHT)));
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
    EXPECT_EQ(lastProgress, FULL_PROGRESS_PERCENT);

    // 6 étapes exécutées
    EXPECT_EQ(startedSteps.size(), TOTAL_PIPELINE_STEPS);
    EXPECT_EQ(completedSteps.size(), TOTAL_PIPELINE_STEPS);
    if (completedSteps.size() >= TOTAL_PIPELINE_STEPS) {
        EXPECT_EQ(completedSteps.front(), 1);
        EXPECT_EQ(completedSteps.back(), LAST_STEP_INDEX);
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

    // Sauvegarde puis relecture avec de nouveaux objets : même chemin que fermeture et réouverture.
    const QString projectPath = QString::fromStdString(IONs::pathToUtf8(rootDirectory / "resume.kikproj"));
    ProjectGrid::State grid;
    grid.tileDirectory = QString::fromStdString(IONs::pathToUtf8(tilesDirectory));
    const QString savedOutput = QString::fromStdString(IONs::pathToUtf8(outputDirectory));
    ASSERT_TRUE(ProjectSerializer::saveToFile(projectPath, QStringLiteral("Reprise"),
                                            savedOutput, 3, grid, nullptr, script));
    ProjectData reopened;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath, &reopened));
    const auto summary = ProjectSerializer::summarize(reopened);
    ASSERT_TRUE(summary.canResume) << summary.text().toStdString();
    EXPECT_EQ(reopened.mode, 3);
    EXPECT_EQ(summary.runStatus, QStringLiteral("paused"));
    EXPECT_EQ(summary.upToDateSteps, QStringList{QStringLiteral("import")});

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

    resumeWorker.runPipeline(QString::fromStdString(reopened.backendScript.dump()),
                             QString(), true, /*isResume=*/true);

    EXPECT_TRUE(resumeFinished);
    EXPECT_TRUE(resumeSuccess) << resumeError.toStdString();
    EXPECT_TRUE(resumeError.isEmpty());

    // L'étape 1 ayant été sautée, les étapes terminées lors de la reprise vont de 2 à 6 (5 étapes restantes)
    EXPECT_EQ(resumedCompletedSteps.size(), RESUMED_COMPLETED_STEPS_COUNT);
    if (!resumedCompletedSteps.empty()) {
        EXPECT_EQ(resumedCompletedSteps.front(), 2);
        EXPECT_EQ(resumedCompletedSteps.back(), LAST_STEP_INDEX);
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
    const QString invalidJson = QString::fromLatin1(R"({ "invalid": true })");
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
    timer.start(ASYNC_WAIT_TIMEOUT_MS);

    runner.startRun();
    EXPECT_TRUE(runner.isRunning());

    loop.exec();

    EXPECT_TRUE(runStartedEmitted);
    EXPECT_TRUE(runFinishedEmitted);
    EXPECT_TRUE(mosaicReadyEmitted);
    EXPECT_EQ(finalProgress, FULL_PROGRESS_PERCENT);
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(fs::exists(outputDirectory / "mosaic.tif"));
}

// 6. Concurrence : l'interface prépare les pyramides en arrière-plan pendant que le pipeline importe les mêmes images.
// Le thread passe par le cache partagé (core/io/pyramid_cache.h), donc écrit exactement les fichiers que lit et écrit
// l'import ; il retire la provenance à chaque passage pour forcer la réécriture pendant tout le calcul.
TEST_F(PipelineWorkerTestClass, ImportAndImmediatePipelineRunConcurrentExecution) {
    const Json script = makeScript(tilesDirectory, outputDirectory, checkpointDirectory);
    const QString scriptText = QString::fromStdString(script.dump());

    std::vector<fs::path> sources;
    for (const auto& entry : fs::directory_iterator(tilesDirectory))
        if (entry.is_regular_file() && entry.path().extension() == ".tif") sources.push_back(entry.path());
    ASSERT_FALSE(sources.empty());
    const fs::path cacheDirectory = IONs::pyramidCacheDirectory(tilesDirectory);

    std::atomic<bool> stopBackgroundThread{false};
    std::thread backgroundPyramidGenerator([&]() {
        constexpr int MAXIMUM_PASSES = 20;
        for (int pass = 0; pass < MAXIMUM_PASSES && !stopBackgroundThread.load(); ++pass) {
            for (const fs::path& source : sources) {
                fs::path provenance = IONs::pyramidCachePath(cacheDirectory, source);
                provenance += ".source.json";
                std::error_code removeError;
                fs::remove(provenance, removeError);
                IONs::PyramidCacheEntry entry;
                std::string errorMessage;
                // Un échec ponctuel est permis (remplacement refusé sous Windows pendant une lecture) : seul compte
                // le résultat du pipeline et l'état final du cache.
                IONs::preparePyramidCache(source, cacheDirectory, entry, errorMessage);
            }
        }
    });

    // En même temps, l'utilisateur lance immédiatement le pipeline
    PipelineWorkerClass worker;
    bool finishedEmitted = false;
    bool finishedSuccess = false;
    QString finishedError;
    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool success, const QString& error, const QImage&) {
        finishedEmitted = true;
        finishedSuccess = success;
        finishedError = error;
    });

    worker.runPipeline(scriptText, QString(), true, false);

    stopBackgroundThread.store(true);
    if (backgroundPyramidGenerator.joinable()) {
        backgroundPyramidGenerator.join();
    }

    EXPECT_TRUE(finishedEmitted);
    EXPECT_TRUE(finishedSuccess) << finishedError.toStdString();
    EXPECT_TRUE(fs::exists(outputDirectory / "mosaic.tif"));

    // Le cache final est cohérent : chaque pyramide est complète et vient de sa source.
    for (const fs::path& source : sources) {
        IONs::PyramidCacheEntry entry;
        std::string errorMessage;
        ASSERT_TRUE(IONs::preparePyramidCache(source, cacheDirectory, entry, errorMessage)) << errorMessage;
        const cv::Mat original = IONs::readImage(source, cv::IMREAD_UNCHANGED);
        EXPECT_EQ(entry.width, original.cols);
        EXPECT_EQ(entry.height, original.rows);
        EXPECT_EQ(entry.type, original.type());
    }
}

// 7. Calcul complet 2x2 jusqu'à l'export
TEST_F(PipelineWorkerTestClass, FullTwoByTwoGridPipelineRunUpToExport) {
    const fs::path dir2x2 = rootDirectory / "tiles_2x2";
    const fs::path output2x2 = rootDirectory / "output_2x2";
    const fs::path checkpoints2x2 = rootDirectory / "checkpoints_2x2";
    fs::create_directories(dir2x2);
    fs::create_directories(output2x2);
    fs::create_directories(checkpoints2x2);

    writeTiles2x2(dir2x2);

    const Json script = makeScript(dir2x2, output2x2, checkpoints2x2);
    const QString scriptText = QString::fromStdString(script.dump());

    PipelineWorkerClass worker;
    bool finishedEmitted = false;
    bool finishedSuccess = false;
    QString finishedError;
    QImage generatedPreview;

    QObject::connect(&worker, &PipelineWorkerClass::pipelineFinished, [&](bool success, const QString& error, const QImage& preview) {
        finishedEmitted = true;
        finishedSuccess = success;
        finishedError = error;
        generatedPreview = preview;
    });

    worker.runPipeline(scriptText, QString(), true, false);

    EXPECT_TRUE(finishedEmitted);
    EXPECT_TRUE(finishedSuccess) << finishedError.toStdString();
    EXPECT_FALSE(generatedPreview.isNull());

    // Vérifie que les 4 tuiles ont bien leurs pyramides dans .cache/pyramids
    const fs::path pyramidDir = dir2x2 / ".cache" / "pyramids";
    EXPECT_TRUE(fs::is_directory(pyramidDir));
    int pyramidCount = 0;
    for (const auto& entry : fs::directory_iterator(pyramidDir)) {
        if (entry.path().extension() == ".tif" && entry.path().filename().string().find("_pyramid") != std::string::npos) {
            EXPECT_GT(fs::file_size(entry.path()), 0U);
            ++pyramidCount;
        }
    }
    EXPECT_EQ(pyramidCount, 4);

    // Vérifie que la mosaïque finale est exportée et valide
    const fs::path mosaicFile = output2x2 / "mosaic.tif";
    ASSERT_TRUE(fs::is_regular_file(mosaicFile));
    EXPECT_GT(fs::file_size(mosaicFile), 0U);

    const cv::Mat mosaic = IONs::readImage(mosaicFile, cv::IMREAD_UNCHANGED);
    EXPECT_FALSE(mosaic.empty());
    EXPECT_GT(mosaic.cols, TILE_WIDTH);
    EXPECT_GT(mosaic.rows, TILE_HEIGHT);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen"); // NOLINT(misc-include-cleaner)
    QApplication app(argc, argv); // NOLINT(misc-include-cleaner)
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
