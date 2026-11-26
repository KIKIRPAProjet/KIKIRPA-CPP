// include/frontend/pipeline_worker.h
#pragma once

#include "pipeline_results.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QMutex>

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
        void runPipeline(const QString& scriptJsonText, const QString& startFromStep = QString(), bool skipCompletedSteps = true, bool isResume = false);
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
        void mosaicFileReady(const QString& imagePath);
        // Paires et lignes de couture calculées, émis avant pipelineFinished ou pipelinePaused.
        void resultsReady(const PipelineResults& results);

    private:
        // Parse et valide la configuration avant exécution.
        bool initializePipeline(const QString& scriptJsonText, const QString& startFromStep, bool skipCompletedSteps, bool isResume);        // Traite chaque ligne émise par le Logger backend et émet les signaux de progression.
        void handleLogMessage(const QString& message, int totalSteps);
        // Analyse l'état de sortie de run() et émet le signal approprié.
        void processExecutionResult(bool success, const std::string& executionError);
        // Construit l'image d'aperçu à partir des données du DataStore.
        QImage generatePreviewImage();
        // Paires et lignes de couture du DataStore ; previewSize vide : pas de calque de couture.
        PipelineResults collectResults(const QSize& previewSize);

        std::unique_ptr<PipelineNs::PipelineClass> pipeline;
        mutable QMutex pipelineMutex;
        std::atomic<bool> pauseRequested{false};
        std::atomic<bool> cancelRequested{false};
};
