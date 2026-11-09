#include "simulationrunner.h"

#include "core/pipeline/pipeline_builder.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <exception>
#include <filesystem>
#include <utility>

SimulationRunner::SimulationRunner(QObject *parent)
    : QObject(parent)
{
}

bool SimulationRunner::isRunning() const
{
    return running;
}

void SimulationRunner::startPipelineRun(const QString &scriptJson, const QString &scriptPath)
{
    if (running) {
        emit statusChanged(tr("Un traitement est déjà en cours."));
        return;
    }

    try {
        const nlohmann::json script = nlohmann::json::parse(scriptJson.toStdString());
        pipeline = std::make_shared<PipelineNs::PipelineClass>(
            PipelineNs::buildPipeline(script));
        pipeline->setScriptPath(std::filesystem::path(scriptPath.toStdString()));
    } catch (const std::exception &exception) {
        emit statusChanged(tr("Le pipeline ne peut pas être chargé : %1")
                               .arg(QString::fromUtf8(exception.what())));
        return;
    }

    pauseRequested = false;
    running = true;
    emit runStarted();
    emit stepChanged(tr("Préparation du pipeline"));
    emit statusChanged(tr("Démarrage du pipeline…"));

    watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished,
            this, &SimulationRunner::finishPipelineRun);
    const std::shared_ptr<PipelineNs::PipelineClass> activePipeline = pipeline;
    watcher->setFuture(QtConcurrent::run([this, activePipeline]() {
        try {
            return activePipeline->run([this](const std::string &message) {
                const QString text = QString::fromStdString(message);
                emit statusChanged(text);
                if (text.startsWith(QStringLiteral("Step ")))
                    emit stepChanged(text);
            });
        } catch (const std::exception &exception) {
            PipelineNs::RunState state = activePipeline->getRunState();
            state.status = PipelineNs::RunStatus::Failed;
            state.error = exception.what();
            activePipeline->setRunState(state);
            emit statusChanged(tr("Le pipeline a échoué : %1")
                                   .arg(QString::fromUtf8(exception.what())));
            return false;
        }
    }));
    emit cancellationAvailable();
}

void SimulationRunner::cancelRun()
{
    if (!running || !pipeline)
        return;
    pauseRequested = true;
    pipeline->requestPause();
    emit statusChanged(tr("Pause demandée ; l'étape en cours va se terminer."));
}

void SimulationRunner::showComparison()
{
    emit comparisonRequested();
}

void SimulationRunner::toggleComparison()
{
    comparisonVisible = !comparisonVisible;
    emit comparisonVisibilityChanged(comparisonVisible);
    emit statusChanged(comparisonVisible
        ? tr("Comparaison affichée — curseur de séparation actif.")
        : tr("Comparaison côte à côte affichée."));
}

void SimulationRunner::keepComparisonA()
{
    emit statusChanged(tr("Résultat A conservé; les étapes suivantes sont à refaire."));
}

void SimulationRunner::keepComparisonB()
{
    emit statusChanged(tr("Résultat B conservé; les étapes suivantes sont à refaire."));
}

void SimulationRunner::finishPipelineRun()
{
    if (!watcher)
        return;
    const bool success = watcher->result();
    const PipelineNs::RunStatus status = pipeline
        ? pipeline->getRunState().status : PipelineNs::RunStatus::Failed;
    watcher->deleteLater();
    watcher = nullptr;
    running = false;
    emit progressAdvanced(success ? 100 : 0);

    if (pauseRequested || status == PipelineNs::RunStatus::Paused) {
        emit stepChanged(tr("En pause"));
        emit statusChanged(tr("Pipeline mis en pause. Les checkpoints permettent de reprendre."));
        emit runCancelled();
    } else if (success) {
        emit stepChanged(tr("Terminé"));
        emit statusChanged(tr("Pipeline terminé."));
    } else {
        emit stepChanged(tr("Échec"));
        emit statusChanged(tr("Le pipeline a échoué. Consultez le journal pour le détail."));
    }
    emit runFinished();
    pipeline.reset();
    pauseRequested = false;
}
