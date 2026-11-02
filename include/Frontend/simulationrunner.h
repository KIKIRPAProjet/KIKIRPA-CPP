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
    void stepChanged(const QString &stepName);
    void runFinished();
    void runCancelled();
    void cancellationAvailable();
    void statusChanged(const QString &message);
    void comparisonRequested();
    void comparisonVisibilityChanged(bool visible);

private:
    void finishRun();

    QTimer *m_timer = nullptr;
    int m_firstStep = 1;
    int m_progress = 0;
    bool m_running = false;
    bool m_comparisonVisible = false;
};

#endif
