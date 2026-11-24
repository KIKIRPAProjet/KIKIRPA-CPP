// include/frontend/pipeline_runner.h
#pragma once

#include "image_layer.h"
#include "pipeline_results.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

class QThread;
class PipelineWorkerClass;

// Façade de contrôle asynchrone du pipeline, gérant le cycle de vie du QThread et du Worker.
class PipelineRunnerClass : public QObject {
    Q_OBJECT

    public:
        explicit PipelineRunnerClass(QObject* parent = nullptr);
        // Interrompt le calcul et attend l'arrêt complet du QThread pour éviter un crash à la destruction.
        ~PipelineRunnerClass() override;

        bool isRunning() const { return running; }
        bool isPaused() const { return paused; }
        int firstStep() const { return firstStepNumber; }

        void setScriptJson(const QString& scriptJsonText) { scriptJson = scriptJsonText; }
        const QString& getScriptJson() const { return scriptJson; }

        void setStartFromStepName(const QString& stepName) { startFromStepName = stepName; }
        const QString& getStartFromStepName() const { return startFromStepName; }

        // Retire les couches lorsque le projet ou ses tuiles changent pendant un calcul.
        void invalidateImageLayers();

    public slots:
        // Démarre l'exécution asynchrone du pipeline backend dans un QThread dédié.
        void startRun(int firstStep = 1, bool isResume = false);
        // Ordonne une mise en pause propre entre deux étapes du pipeline.
        void pauseRun();
        // Reprend l'exécution là où elle a été interrompue en réutilisant les checkpoints.
        void resumeRun();
        // Demande l'arrêt après l'étape en cours, ou annule un pipeline déjà en pause.
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
        void statusChanged(const QString& message);
        void comparisonRequested();
        void comparisonVisibilityChanged(bool visible);
        void logMessage(const QString& message);
        void logMessageWithSeverity(const QString& message, const QString& severity);
        void mosaicReady(const QImage& image);
        void imageLayersReady(const QVector<ImageLayer>& layers, const QRectF& bounds);
        void mosaicFileReady(const QString& imagePath);
        void resultsReady(const PipelineResults& results);
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
        bool imageLayersInvalidated = false;

};
