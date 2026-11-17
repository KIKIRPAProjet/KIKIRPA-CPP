// include/frontend/simulationrunner.h
#pragma once

#include <QObject>
#include <QString>

#include <memory>

template <typename T>
class QFutureWatcher;

namespace PipelineNs {
class PipelineClass;
}

// Exécute le pipeline en tâche de fond et transmet son état à l'interface Qt.
class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    // Construit le contrôleur d'exécution associé à son parent Qt.
    explicit SimulationRunner(QObject *parent = nullptr);

    // Demande une pause du pipeline et attend la fin de la tâche de fond.
    ~SimulationRunner() override;

    // Indique si un pipeline est en cours d'exécution.
    bool isRunning() const;

public slots:
    // Lance le pipeline depuis la première étape et archive ses fichiers sous runDirectory.
    // Une reprise ultérieure à la première étape est refusée sans checkpoint valide.
    void startRun(int firstStep, const QString &scriptPath,
                  const QString &runDirectory, const QString &projectName,
                  const QString &executionId);

    // Demande l'arrêt entre deux steps.
    void cancelRun();

    // Demande l'affichage du dialogue de comparaison.
    void showComparison();

    // Modifie la visibilité de la comparaison ou conserve l'un des résultats.
    void toggleComparison();
    void keepComparisonA();
    void keepComparisonB();

signals:
    // Signale le début, l'avancement et la fin ou l'échec du pipeline.
    void runStarted();
    void progressAdvanced(int percentage);
    void stepChanged(int stepNumber, const QString &stepName);
    void runFinished();
    void runCancelled();
    void runFailed(const QString &message);
    void cancellationAvailable();
    void statusChanged(const QString &message);

    // Transmet les événements de comparaison et les messages du pipeline à l'interface.
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);
    void logMessage(const QString &message);

private:
    // Surveille le travailleur et garde le pipeline accessible pour l'annulation.
    QFutureWatcher<bool> *watcher = nullptr;
    std::shared_ptr<PipelineNs::PipelineClass> pipeline;

    // État de l'exécution et de l'affichage de comparaison.
    bool running = false;
    bool comparisonVisible = false;
};
