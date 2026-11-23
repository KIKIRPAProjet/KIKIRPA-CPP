#ifndef MENUBAR_MANAGER_H
#define MENUBAR_MANAGER_H

#include <QObject>

class QMenuBar;

class MenuBarManager : public QObject
{
    Q_OBJECT

public:
    explicit MenuBarManager(QObject *parent = nullptr);

    void setupMenus(QMenuBar *menuBar);

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
    void comparisonRequested();
    void aboutRequested();
};

#endif
