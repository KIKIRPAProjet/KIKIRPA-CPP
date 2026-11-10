/*
 * Header : le QUOI
 * Orchestre l'exécution du pipeline d'assemblage (backend réel via QtConcurrent
 * ou démonstration temporisée), sa mise en pause/annulation et sa reprise.
 * - ce que ça fait : lance le pipeline dans un thread de travail, transmet la
 *   progression et les journaux à l'interface, demande une pause propre entre
 *   deux étapes (requestPause) et reprend depuis les checkpoints sauvegardés ;
 * - ce que ça renvoie : l'état courant via les accesseurs et des signaux Qt ;
 * - sécurité multi-thread : requestPause() est atomique côté backend et les
 *   journaux sont rapatriés sur le thread UI par file d'attente (QueuedConnection).
 */
#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace PipelineNs {
    class PipelineClass;
}

class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(QObject *parent = nullptr);
    ~SimulationRunner() override;

    // Indique si une exécution (backend ou démonstration) est actuellement active.
    bool isRunning() const;

    // Indique si la dernière exécution a été interrompue proprement (statut en pause).
    bool isPaused() const;

    // Indique s'il est possible de reprendre l'assemblage depuis un état sauvegardé.
    bool canResume() const;

    // Indique si une annulation est en attente de la fin de l'étape backend en cours.
    bool isCancellationPending() const;

    // Progression courante en pourcentage [0, 100].
    int currentProgress() const;

    // Chemin du script JSON backend associé à l'exécution courante (vide en mode démo).
    QString backendScriptPath() const;

    // Associe un script backend et inspecte son champ "run" pour savoir si une reprise est prête.
    void setBackendScriptPath(const QString &scriptPath);
    void clearBackendScriptPath();

public slots:
    // Lance la démonstration temporisée (ou le pipeline backend si un script est déjà associé).
    void startRun(int firstStep = 1);

    // Exécute le vrai pipeline backend décrit par scriptPath à partir de firstStep (1-based).
    void startBackendRun(const QString &scriptPath, int firstStep = 1);

    // Reprend l'assemblage mis en pause (ou en échec partiel) à partir des checkpoints validés.
    void resumeRun();

    // Demande l'annulation propre : appelle PipelineClass::requestPause() entre deux étapes.
    void cancelRun();

    void showComparison();
    void toggleComparison();
    void keepComparisonA();
    void keepComparisonB();

signals:
    void runStarted();
    void progressAdvanced(int percentage);
    void stepChanged(const QString &stepName);
    void runFinished();
    void runPaused();
    void runCancelled();
    void runFailed(const QString &errorMessage);
    void cancellationAvailable();
    void cancellationRequested();
    void resumeAvailabilityChanged(bool available);
    void backendLogReceived(const QString &message, const QString &level);
    void statusChanged(const QString &message);
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);

private slots:
    void onBackendRunFinished();

private:
    enum class StartMode {
        FromBeginning,
        FromStep,
        Resume
    };

    void launchBackendPipeline(const QString &scriptPath, StartMode mode, int firstStep);
    void startDemoTimer(bool resuming);
    void handleBackendLogMessage(const QString &message, int totalSteps);
    void finishDemoRun();
    bool inspectScriptResumable(const QString &scriptPath, int *completedStepsOut = nullptr,
                                int *totalStepsOut = nullptr) const;
    void setResumeAvailable(bool available);

    QTimer *timer = nullptr;
    QFutureWatcher<bool> *backendWatcher = nullptr;
    mutable QMutex pipelineMutex;
    std::shared_ptr<PipelineNs::PipelineClass> activePipeline;
    QString scriptPath;
    QString lastBackendError;

    int firstStepNumber = 1;
    int progress = 0;
    bool running = false;
    bool backendRunning = false;
    bool paused = false;
    bool resumeAvailable = false;
    bool cancellationPending = false;
    bool comparisonVisible = false;
};

#endif
