#ifndef MENUBAR_MANAGER_H
#define MENUBAR_MANAGER_H

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
    void setCancelAvailable(bool available);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void importTilesRequested();
    void saveProjectRequested();
    void exportRequested();
    void homeRequested();
    void quitRequested();
    void undoRequested();
    void redoRequested();
    void togglePanelsRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void fitToWindowRequested();
    void modeChangeRequested(int mode);
    void runRequested(int startStep);
    void resumeRequested();
    void cancelRunRequested();
    void comparisonRequested();
    void aboutRequested();

private:
    QAction *m_resumeAction = nullptr;
    QAction *m_cancelAction = nullptr;
};

#endif
