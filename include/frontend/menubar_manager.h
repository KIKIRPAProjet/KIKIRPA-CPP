#pragma once

#include <QObject>

class QAction;
class QMenuBar;

class MenuBarManager : public QObject
{
    Q_OBJECT

public:
    explicit MenuBarManager(QObject *parent = nullptr);

    void setupMenus(QMenuBar *menuBar);
    void setResumeAvailable(bool available);
    void setPauseAvailable(bool available);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void importTilesRequested();
    void saveProjectRequested();
    void exportRequested();
    void homeRequested();
    void quitRequested();
    void togglePanelsRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void fitToWindowRequested();
    void runRequested(int startStep);
    void resumeRequested();
    void pauseRunRequested();
    void comparisonRequested();
    void aboutRequested();

private:
    QAction *m_resumeAction = nullptr;
    QAction *m_pauseAction = nullptr;
};
