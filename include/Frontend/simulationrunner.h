#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

#include <QObject>
#include <QString>

class QTimer;

class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(QObject *parent = nullptr);

    bool isRunning() const;

public slots:
    void startRun(int firstStep);
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
    void cancellationAvailable();
    void statusChanged(const QString &message);
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);
    void logMessage(const QString &message);
    void mosaicReady(const QImage &image);

private:
    void finishRun();

    QTimer *timer = nullptr;
    int firstStepNumber = 1;
    int progress = 0;
    bool running = false;
    bool comparisonVisible = false;
};

#endif
