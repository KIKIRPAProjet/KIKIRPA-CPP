// include/frontend/simulationrunner.h
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QThread;
class PipelineWorkerClass;

// Façade de contrôle asynchrone du pipeline, gérant le cycle de vie du QThread et du Worker.
class SimulationRunner : public QObject {
    Q_OBJECT

    public:
        explicit SimulationRunner(QObject* parent = nullptr);
        // Interrompt le calcul et attend l'arrêt complet du QThread pour éviter un crash à la destruction.
        ~SimulationRunner() override;

        bool isRunning() const { return running; }
        bool isPaused() const { return paused; }
        int firstStep() const { return firstStepNumber; }
        int pausedStep() const { return pausedStepNumber; }
        void resetPausedState() { paused = false; pausedStepNumber = 0; }

        void setScriptJson(const QString& scriptJsonText) { scriptJson = scriptJsonText; }
        const QString& getScriptJson() const { return scriptJson; }

        void setStartFromStepName(const QString& stepName) { startFromStepName = stepName; }
        const QString& getStartFromStepName() const { return startFromStepName; }

    public slots:
        // Démarre l'exécution asynchrone du pipeline backend dans un QThread dédié.
        void startRun(int firstStep = 1);
        // Ordonne une mise en pause propre entre deux étapes du pipeline.
        void pauseRun();
        // Reprend l'exécution là où elle a été interrompue en réutilisant les checkpoints.
        void resumeRun();
        // Annule et interrompt immédiatement l'exécution.
        void cancelRun();

        void showComparison();
        void toggleComparison();
        void keepComparisonA();
        void keepComparisonB();

    signals:
        void runStarted();
        void progressAdvanced(int percentage);
        void stepChanged(int stepNumber, const QString& stepName);
        void runFinished();
        void runCancelled();
        void cancellationAvailable();
        void statusChanged(const QString& message);
        void comparisonRequested();
        void comparisonVisibilityChanged(bool visible);
        void logMessage(const QString& message);
        void logMessageWithSeverity(const QString& message, const QString& severity);
        void mosaicReady(const QImage& image);
        void pausedStateChanged(bool isPaused);

    private:
        // Connecte les signaux d'avancement et de journalisation du worker vers l'UI.
        void setupProgressConnections();
        // Connecte les signaux d'état final (pause, arrêt, succès, échec).
        void setupStateConnections();
        // Traite la fin du calcul et propage la prévisualisation finale.
        void handleWorkerFinished(bool success, const QString& errorText, const QImage& previewImage);
        // Libère proprement le thread et l'instance du worker.
        void cleanUpThread();

        QThread* workerThread = nullptr;
        PipelineWorkerClass* worker = nullptr;

        QString scriptJson;
        QString startFromStepName;
        int firstStepNumber = 1;
        int progress = 0;
        bool running = false;
        bool paused = false;
        bool comparisonVisible = false;
        int pausedStepNumber = 0;

};
