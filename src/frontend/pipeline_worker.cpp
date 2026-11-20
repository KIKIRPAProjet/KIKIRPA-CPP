// src/frontend/pipeline_worker.cpp
#include "pipeline_worker.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/io/unicode_path.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "frontendutils.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

    // Expression régulière pour décoder les messages backend : "Step X/Y OK" ou "Step X/Y failed".
    const QRegularExpression STEP_PROGRESS_REGEX(QStringLiteral(R"(Step\s+(\d+)/(\d+)\s+([A-Za-z]+))"));

    // Largeur cible de la vignette d'aperçu pour préserver les performances de l'UI.
    constexpr int PREVIEW_TARGET_WIDTH = 1200;

    // Catégorise le message backend afin de router correctement les filtres dans le LogPanel.
    QString extractSeverity(const QString& message) {
        if (message.contains(QStringLiteral("warning"), Qt::CaseInsensitive)) {
            return QStringLiteral("warning");
        }
        if (message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
            message.startsWith(QStringLiteral("error"), Qt::CaseInsensitive) ||
            message.startsWith(QStringLiteral("Pipeline error"), Qt::CaseInsensitive)) {
            return QStringLiteral("error");
        }
        return QStringLiteral("info");
    }

    // Boîte englobante de l'ensemble des tuiles repositionnées dans le repère absolu.
    struct BoundingBox {
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();
        double width() const { return maxX - minX; }
        double height() const { return maxY - minY; }
        bool isValid() const { return width() > 0.0 && height() > 0.0; }
    };

    // Détermine l'étendue totale de la mosaïque à partir des coordonnées calculées par absolute-pos.
    BoundingBox computeBoundingBox(const std::vector<DataNs::TilePositionData>& positions,
                                   const DataNs::TileList& tiles) {
        BoundingBox box;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto& pos = positions[i].position;
            const int width = tiles[i]->getLevel(0).width;
            const int height = tiles[i]->getLevel(0).height;
            box.minX = std::min(box.minX, pos.x);
            box.minY = std::min(box.minY, pos.y);
            box.maxX = std::max(box.maxX, pos.x + width);
            box.maxY = std::max(box.maxY, pos.y + height);
        }
        return box;
    }

    // Dessine une tuile redimensionnée à sa position absolue relative sur le canvas de prévisualisation.
    void drawTileOnCanvas(QPainter& painter, const DataNs::TileClass& tile,
                          const cv::Point2d& position, const BoundingBox& box, double scale) {
        QImage tileImage;
        const auto& tiffPath = tile.getTiff();
        if (!tiffPath.empty() && std::filesystem::exists(tiffPath)) {
            // Utilise le TIFF pyramidal : on sélectionne le niveau réduit le plus approprié
            IONs::PyramidalTiffReaderClass reader(tiffPath);
            int chosenLevel = DataNs::LEVEL_COUNT - 1;
            for (int l = chosenLevel; l >= 0; --l) {
                if (tile.getLevel(l).width * scale >= 100 || l == 0) {
                    chosenLevel = l;
                    break;
                }
            }
            cv::Mat levelMat;
            std::string err;
            if (reader.readLevel(chosenLevel, levelMat, err)) {
                tileImage = FrontendUtils::matToQImage(levelMat);
            }
        }

        // Repli sur l'image source si le TIFF pyramidal n'a pas pu être lu
        if (tileImage.isNull()) {
            const QString sourcePath = QString::fromStdString(IONs::pathToUtf8(tile.getSource()));
            const int previewW = qMax(1, static_cast<int>(tile.getLevel(0).width * scale));
            const int previewH = qMax(1, static_cast<int>(tile.getLevel(0).height * scale));
            tileImage = readPreviewImage(sourcePath, QSize(previewW, previewH));
            if (tileImage.isNull()) return;
        }

        const int destX = static_cast<int>((position.x - box.minX) * scale);
        const int destY = static_cast<int>((position.y - box.minY) * scale);
        const int destWidth = static_cast<int>(tile.getLevel(0).width * scale);
        const int destHeight = static_cast<int>(tile.getLevel(0).height * scale);
        painter.drawImage(QRect(destX, destY, destWidth, destHeight), tileImage);
    }

    // Assemble une mosaïque composite basse résolution pour l'émission vers ArtworkCanvas (mode Export).
    QImage renderPositionsMosaic(const std::vector<DataNs::TilePositionData>& positions,
                                 const DataNs::TileList& tiles) {
        if (positions.empty() || positions.size() != tiles.size()) return QImage();

        const BoundingBox box = computeBoundingBox(positions, tiles);
        if (!box.isValid()) return QImage();

        const double scale = static_cast<double>(PREVIEW_TARGET_WIDTH) / box.width();
        const int targetHeight = qMax(100, static_cast<int>(box.height() * scale));

        QImage canvas(PREVIEW_TARGET_WIDTH, targetHeight, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::black);

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            drawTileOnCanvas(painter, *tiles[i], positions[i].position, box, scale);
        }
        painter.end();
        return canvas;
    }

    // Solution de repli si le pipeline s'arrête avant le calcul des positions (affiche la première tuile).
    QImage renderFallbackPreview(const DataNs::TileList& tiles) {
        if (tiles.empty()) return QImage();
        const auto& firstTile = tiles.front();
        if (!firstTile->getTiff().empty() && std::filesystem::exists(firstTile->getTiff())) {
            IONs::PyramidalTiffReaderClass reader(firstTile->getTiff());
            cv::Mat levelMat;
            std::string err;
            const int level = qMin(2, DataNs::LEVEL_COUNT - 1);
            if (reader.readLevel(level, levelMat, err)) {
                const QImage img = FrontendUtils::matToQImage(levelMat);
                if (!img.isNull()) return img;
            }
        }
        const QString firstPath = QString::fromStdString(IONs::pathToUtf8(firstTile->getSource()));
        return readPreviewImage(firstPath, QSize(PREVIEW_TARGET_WIDTH, PREVIEW_TARGET_WIDTH));
    }

}

PipelineWorkerClass::PipelineWorkerClass(QObject* parent)
    : QObject(parent) {
}

PipelineWorkerClass::~PipelineWorkerClass() {
    requestCancel();
}

void PipelineWorkerClass::requestPause() {
    pauseRequested.store(true);
    QMutexLocker locker(&pipelineMutex);
    if (pipeline) pipeline->requestPause();
}

void PipelineWorkerClass::requestCancel() {
    cancelRequested.store(true);
    QMutexLocker locker(&pipelineMutex);
    if (pipeline) pipeline->requestPause();
}

bool PipelineWorkerClass::initializePipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps, bool isResume) {
    nlohmann::json scriptJson;
    try {
        scriptJson = nlohmann::json::parse(scriptJsonText.toStdString());
        auto builtPipeline = std::make_unique<PipelineNs::PipelineClass>(PipelineNs::buildPipeline(scriptJson));
        {
            QMutexLocker locker(&pipelineMutex);
            pipeline = std::move(builtPipeline);
        }
    } catch (const std::exception& exception) {
        emit pipelineFinished(false, tr("Erreur pipeline : %1").arg(exception.what()), QImage());
        return false;
    }

    // Applique les consignes de reprise sur checkpoints (skipCompletedSteps) et de redémarrage ciblé.
    PipelineNs::RunState runState = pipeline->getRunState();
    runState.skipCompletedSteps = skipCompletedSteps;
    if (!startFromStep.isEmpty()) runState.startFromStep = startFromStep.toStdString();

    if (!runState.checkpointDirectory.empty()) {
        namespace fs = std::filesystem;
        const fs::path checkpointDir = fs::absolute(IONs::pathFromUtf8(runState.checkpointDirectory));
        const fs::path statePath = checkpointDir / "run.json";

        // Définit le fichier où le pipeline réécrira le script et l'état après chaque étape
        pipeline->setScriptPath(statePath);

        // En cas de reprise, relit les étapes déjà achevées depuis run.json
        if (isResume && fs::exists(statePath)) {
            std::ifstream input(statePath, std::ios::binary);
            if (input) {
                try {
                    const nlohmann::json stateJson = nlohmann::json::parse(input);
                    if (stateJson.contains("run")) {
                        runState.completedSteps = PipelineNs::RunState::fromJson(stateJson.at("run")).completedSteps;
                    }
                } catch (const std::exception& exception) {
                    emit logMessageReceived(
                        tr("Avertissement lecture run.json : %1").arg(exception.what()),
                        QStringLiteral("warning"));
                }
            }
        }
    }

    pipeline->setRunState(runState);
    return true;
}

void PipelineWorkerClass::handleLogMessage(const QString& message, int totalSteps) {
    emit logMessageReceived(message, extractSeverity(message));

    // Décode le format standard des messages backend : "Step X/Y OK" ou "Step X/Y failed"
    // La progression n'est pas temporisée : elle reflète l'achèvement effectif de chaque algorithme.
    const QRegularExpressionMatch match = STEP_PROGRESS_REGEX.match(message);
    if (!match.hasMatch()) return;

    const int stepIndex = match.captured(1).toInt();
    const int total = match.captured(2).toInt();
    const QString status = match.captured(3);

    emit progressChanged((stepIndex * 100) / qMax(1, total));
    if (status.compare(QLatin1String("OK"), Qt::CaseInsensitive) == 0) {
        emit stepCompleted(stepIndex, total, QString());
        if (stepIndex < total) emit stepStarted(stepIndex + 1, total, QString());
    }
}

void PipelineWorkerClass::processExecutionResult(bool success, const std::string& executionError) {
    if (cancelRequested.load()) {
        emit pipelineCancelled();
        return;
    }

    // Si une pause a été demandée, le pipeline s'arrête proprement avec le statut Paused.
    if (pipeline->getRunState().status == PipelineNs::RunStatus::Paused) {
        const auto& completed = pipeline->getRunState().completedSteps;
        const int count = static_cast<int>(completed.size());
        const QString name = count > 0 ? QString::fromStdString(completed.back()) : QString();
        emit pipelinePaused(count, name);
        return;
    }

    if (success) {
        const auto& dataStore = pipeline->getDataStore();

        if (dataStore.has("mosaic")) {
            const auto& mosaic = dataStore.get<DataNs::Mosaic>("mosaic");

            if (!mosaic.imagePath.empty()) {
                emit mosaicFileReady(
                    QString::fromStdString(IONs::pathToUtf8(mosaic.imagePath)));
            }
        }
        emit progressChanged(100);
        emit pipelineFinished(true, QString(), generatePreviewImage());
        return;
    }

    const QString errorText = executionError.empty()
        ? tr("Échec de l'exécution d'une étape du pipeline.")
        : QString::fromStdString(executionError);
    emit pipelineFinished(false, errorText, QImage());
}

void PipelineWorkerClass::runPipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps, bool isResume) {
    pauseRequested.store(false);
    cancelRequested.store(false);

    if (!initializePipeline(scriptJsonText, startFromStep, skipCompletedSteps, isResume)) return;

    const int totalSteps = static_cast<int>(pipeline->getSteps().size());
    emit started();
    emit progressChanged(0);
    if (totalSteps > 0) emit stepStarted(1, totalSteps, QString());

    // Le callback capture les lignes de sortie et les transmet à l'UI sans bloquer le calcul.
    auto logger = [this, totalSteps](const std::string& raw) {
        handleLogMessage(QString::fromStdString(raw), totalSteps);
    };

    bool success = false;
    std::string executionError;
    try {
        success = pipeline->run(logger);
    } catch (const std::exception& exception) {
        executionError = exception.what();
    }

    processExecutionResult(success, executionError);
}

QImage PipelineWorkerClass::generatePreviewImage() {
    if (!pipeline) return QImage();
    const auto& dataStore = pipeline->getDataStore();

    // Priorité à l'assemblage calculé via les positions finales et les tuiles importées.
    if (dataStore.has("positions") && dataStore.has("tiles")) {
        try {
            const auto& positions = dataStore.get<std::vector<DataNs::TilePositionData>>("positions");
            const auto& tiles = dataStore.get<DataNs::TileList>("tiles");
            const QImage mosaic = renderPositionsMosaic(positions, tiles);
            if (!mosaic.isNull()) return mosaic;
        } catch (const std::exception& exception) {
            emit logMessageReceived(tr("Avertissement rendu mosaïque : %1").arg(exception.what()), QStringLiteral("warning"));
        }
    }

    // Repli : première tuile si seules les tuiles sont présentes sans positionnement.
    if (dataStore.has("tiles")) {
        try {
            return renderFallbackPreview(dataStore.get<DataNs::TileList>("tiles"));
        } catch (const std::exception& exception) {
            emit logMessageReceived(tr("Avertissement rendu de repli : %1").arg(exception.what()), QStringLiteral("warning"));
        }
    }
    return QImage();
}
