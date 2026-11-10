// include/frontend/simulationrunner.h
#pragma once

#include <QObject>
#include <QString>

#include <memory>

template <typename T>
class QFutureWatcher;

class QImage;

namespace PipelineNs {
class PipelineClass;
}

class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(QObject *parent = nullptr);
    ~SimulationRunner() override;

    bool isRunning() const;

public slots:
    void startRun(int firstStep, const QString &scriptPath,
                  const QString &runDirectory, const QString &projectName,
                  const QString &executionId);
    void cancelRun();
    void showComparison();
    void toggleComparison();
    void keepComparisonA();
    void keepComparisonB();

signals:
    void runStarted();
    void progressAdvanced(int percentage);
    void stepChanged(int stepNumber, const QString &stepName);
    void runFinished();
    void runCancelled();
    void runFailed(const QString &message);
    void cancellationAvailable();
    void statusChanged(const QString &message);
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);
    void logMessage(const QString &message);
    void mosaicReady(const QImage &image);

private:
    QFutureWatcher<bool> *watcher = nullptr;
    std::shared_ptr<PipelineNs::PipelineClass> pipeline;
    bool running = false;
    bool comparisonVisible = false;
};
