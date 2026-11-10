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

class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(QObject *parent = nullptr);
    ~SimulationRunner() override;

    bool isRunning() const;

public slots:
    void startPipelineRun(const QString &scriptJson, const QString &scriptPath);
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
    void runCancelled();
    void cancellationAvailable();
    void statusChanged(const QString &message);
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);

private:
    void finishPipelineRun();

    QFutureWatcher<bool> *watcher = nullptr;
    std::shared_ptr<PipelineNs::PipelineClass> pipeline;
    bool running = false;
    bool pauseRequested = false;
    bool comparisonVisible = false;
};
