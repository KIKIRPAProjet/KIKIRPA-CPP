// src/frontend/pipeline_worker.cpp
#include "pipeline_worker.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

    // Largeur cible de la vignette d'aperçu pour préserver les performances de l'UI.
    constexpr int PREVIEW_TARGET_WIDTH = 1200;

    // Catégorise le message backend afin de router correctement les filtres dans le LogPanel.
    QString extractSeverity(const QString& message) {
        if (message.contains(QStringLiteral("warning"), Qt::CaseInsensitive)) {
            return QStringLiteral("warning");
        }
        if (message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
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
        const QString sourcePath = QString::fromStdString(tile.getSource().string());
        const QImage tileImage(sourcePath);
        if (tileImage.isNull()) return;

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
        const QString firstPath = QString::fromStdString(tiles.front()->getSource().string());
        return QImage(firstPath);
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
    // requestPause() est thread-safe et ordonne l'arrêt à la frontière entre deux steps.
    if (pipeline) pipeline->requestPause();
}

void PipelineWorkerClass::requestCancel() {
    cancelRequested.store(true);
    if (pipeline) pipeline->requestPause();
}

bool PipelineWorkerClass::initializePipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps) {
    nlohmann::json scriptJson;
    try {
        scriptJson = nlohmann::json::parse(scriptJsonText.toStdString());
        pipeline = std::make_unique<PipelineNs::PipelineClass>(PipelineNs::buildPipeline(scriptJson));
    } catch (const std::exception& exception) {
        emit pipelineFinished(false, tr("Erreur pipeline : %1").arg(exception.what()), QImage());
        return false;
    }

    // Applique les consignes de reprise sur checkpoints (skipCompletedSteps) et de redémarrage ciblé.
    PipelineNs::RunState runState = pipeline->getRunState();
    runState.skipCompletedSteps = skipCompletedSteps;
    if (!startFromStep.isEmpty()) runState.startFromStep = startFromStep.toStdString();
    pipeline->setRunState(runState);
    return true;
}

void PipelineWorkerClass::handleLogMessage(const QString& message, int totalSteps) {
    emit logMessageReceived(message, extractSeverity(message));

    // Décode le format standard des messages backend : "Step X/Y OK" ou "Step X/Y failed"
    // La progression n'est pas temporisée : elle reflète l'achèvement effectif de chaque algorithme.
    static const QRegularExpression regex(QStringLiteral(R"(Step\s+(\d+)/(\d+)\s+([A-Za-z]+))"));
    const QRegularExpressionMatch match = regex.match(message);
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
        emit progressChanged(100);
        emit pipelineFinished(true, QString(), generatePreviewImage());
        return;
    }

    const QString errorText = executionError.empty()
        ? tr("Échec de l'exécution d'une étape du pipeline.")
        : QString::fromStdString(executionError);
    emit pipelineFinished(false, errorText, QImage());
}

void PipelineWorkerClass::runPipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps) {
    pauseRequested.store(false);
    cancelRequested.store(false);

    if (!initializePipeline(scriptJsonText, startFromStep, skipCompletedSteps)) return;

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
        } catch (const std::exception&) {
        }
    }

    // Repli : première tuile si seules les tuiles sont présentes sans positionnement.
    if (dataStore.has("tiles")) {
        try {
            return renderFallbackPreview(dataStore.get<DataNs::TileList>("tiles"));
        } catch (const std::exception&) {
        }
    }
    return QImage();
}
