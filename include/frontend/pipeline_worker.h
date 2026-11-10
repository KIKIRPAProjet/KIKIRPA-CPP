// include/frontend/pipeline_worker.h
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <string>

namespace PipelineNs {
    class PipelineClass;
}

// Exécuteur asynchrone du pipeline backend, destiné à vivre dans un QThread dédié.
class PipelineWorkerClass : public QObject {
    Q_OBJECT

    public:
        explicit PipelineWorkerClass(QObject* parent = nullptr);
        ~PipelineWorkerClass() override;

        bool isPaused() const { return pauseRequested.load(); }
        bool isCancelled() const { return cancelRequested.load(); }

    public slots:
        // Construit et lance le pipeline à partir du script JSON fourni.
        void runPipeline(const QString& scriptJsonText, const QString& startFromStep = QString(), bool skipCompletedSteps = true);
        // Demande une pause propre entre deux étapes via PipelineClass::requestPause().
        void requestPause();
        // Demande l'arrêt d'urgence du pipeline en cours.
        void requestCancel();

    signals:
        void started();
        void stepStarted(int stepIndex, int totalSteps, const QString& stepName);
        void stepCompleted(int stepIndex, int totalSteps, const QString& stepName);
        void progressChanged(int percentage);
        void logMessageReceived(const QString& message, const QString& severity);
        void pipelinePaused(int lastCompletedStepIndex, const QString& lastCompletedStepName);
        void pipelineFinished(bool success, const QString& errorMessage, const QImage& previewImage);
        void pipelineCancelled();

    private:
        // Parse et valide la configuration avant exécution.
        bool initializePipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps);
        // Traite chaque ligne émise par le Logger backend et émet les signaux de progression.
        void handleLogMessage(const QString& message, int totalSteps);
        // Analyse l'état de sortie de run() et émet le signal approprié.
        void processExecutionResult(bool success, const std::string& executionError);
        // Construit l'image d'aperçu à partir des données du DataStore.
        QImage generatePreviewImage();

        std::unique_ptr<PipelineNs::PipelineClass> pipeline;
        std::atomic<bool> pauseRequested{false};
        std::atomic<bool> cancelRequested{false};

};
