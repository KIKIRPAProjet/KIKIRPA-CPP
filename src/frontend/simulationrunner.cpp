// src/frontend/simulationrunner.cpp
#include "simulationrunner.h"

#include "core/pipeline/pipeline_builder.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path filesystemPath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

}

SimulationRunner::SimulationRunner(QObject *parent)
    : QObject(parent)
{
    watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool succeeded = watcher->result();
        const PipelineNs::RunStatus status = pipeline->getRunState().status;
        running = false;
        pipeline.reset();

        if (status == PipelineNs::RunStatus::Paused) {
            emit stepChanged(0, tr("Annulée"));
            emit statusChanged(tr("Exécution interrompue entre deux étapes."));
            emit runCancelled();
        } else if (succeeded) {
            emit progressAdvanced(100);
            emit stepChanged(0, tr("Terminé"));
            emit statusChanged(tr("Exécution terminée. Le rapport est archivé."));
            emit runFinished();
        } else {
            emit stepChanged(0, tr("Échec"));
            emit statusChanged(tr("L'exécution a échoué. Consultez le rapport archivé pour le motif."));
            emit runFailed(tr("Le pipeline a échoué. Le rapport d'exécution contient le motif disponible."));
        }
    });
}

SimulationRunner::~SimulationRunner()
{
    if (running && pipeline)
        pipeline->requestPause();
    if (watcher && watcher->isRunning())
        watcher->waitForFinished();
}

bool SimulationRunner::isRunning() const
{
    return running;
}

void SimulationRunner::startRun(int firstStep, const QString &scriptPath,
                                const QString &runDirectory, const QString &projectName,
                                const QString &executionId)
{
    if (running) {
        emit statusChanged(tr("Une exécution est déjà en cours."));
        return;
    }
    if (firstStep != 1) {
        emit runFailed(tr("La reprise à partir d'une étape nécessite un checkpoint valide. "
                          "Relancez le pipeline depuis la première étape."));
        return;
    }

    try {
        const std::filesystem::path runPath = filesystemPath(runDirectory);
        std::filesystem::create_directories(runPath);
        const std::filesystem::path sourceScript = filesystemPath(scriptPath);
        std::ifstream source(sourceScript, std::ios::binary);
        if (!source)
            throw std::runtime_error("Impossible de lire le script backend.");
        const PipelineNs::Json sourceJson = PipelineNs::Json::parse(source);
        if (!sourceJson.is_object() || !sourceJson.contains("pipeline"))
            throw std::runtime_error("Le script backend ne contient pas de pipeline.");

        const std::filesystem::path archivedConfig = runPath / "pipeline.json";
        {
            std::ofstream output(archivedConfig, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("Impossible d'archiver la configuration du pipeline.");
            output << sourceJson.dump(2) << '\n';
            if (!output)
                throw std::runtime_error("Impossible d'écrire la configuration archivée.");
        }
        auto newPipeline = std::make_shared<PipelineNs::PipelineClass>(
            PipelineNs::buildPipeline(sourceJson));
        PipelineNs::RunState runState;
        runState.checkpointDirectory = (runPath / "checkpoints").u8string();
        newPipeline->setRunState(runState);
        newPipeline->setScriptPath(runPath / "run.json");
        newPipeline->setReportPath(runPath / "execution.json");
        newPipeline->setReportMetadata({
            {"projectName", projectName.toUtf8().toStdString()},
            {"executionId", executionId.toUtf8().toStdString()},
            {"project", sourceJson.value("project", PipelineNs::Json::object())}
        });

        pipeline = std::move(newPipeline);
        running = true;
        emit runStarted();
        emit statusChanged(tr("Exécution du pipeline réel…"));
        emit cancellationAvailable();

        const std::shared_ptr<PipelineNs::PipelineClass> currentPipeline = pipeline;
        watcher->setFuture(QtConcurrent::run([this, currentPipeline]() {
            try {
                return currentPipeline->run([this, currentPipeline](const std::string &message) {
                    const QString statusMessage = QString::fromUtf8(message.c_str());
                    emit statusChanged(statusMessage);
                    emit logMessage(statusMessage);

                    std::size_t stepNumber = 0;
                    std::size_t stepCount = 0;
                    if (std::sscanf(message.c_str(), "Step %zu/%zu", &stepNumber, &stepCount) == 2 &&
                        stepNumber > 0 && stepNumber <= currentPipeline->getSteps().size()) {
                        const auto &step = currentPipeline->getSteps()[stepNumber - 1];
                        const PipelineNs::Json config = step->getConfigJson();
                        emit stepChanged(static_cast<int>(stepNumber),
                                         QString::fromStdString(config.value("name", std::string())));
                        if (stepCount > 0)
                            emit progressAdvanced(static_cast<int>((stepNumber - 1) * 100 / stepCount));
                        if (message.find(" OK ") != std::string::npos && stepCount > 0)
                            emit progressAdvanced(static_cast<int>(stepNumber * 100 / stepCount));
                    }
                });
            } catch (const std::exception &exception) {
                emit statusChanged(tr("Erreur du pipeline : %1")
                                       .arg(QString::fromUtf8(exception.what())));
                return false;
            }
        }));
    } catch (const std::exception &exception) {
        pipeline.reset();
        running = false;
        const QString message = QString::fromUtf8(exception.what());
        emit statusChanged(tr("Impossible de démarrer le pipeline : %1").arg(message));
        emit runFailed(message);
    }
}

void SimulationRunner::cancelRun()
{
    if (!running || !pipeline)
        return;
    pipeline->requestPause();
    emit statusChanged(tr("Annulation demandée ; l'étape en cours va se terminer."));
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
