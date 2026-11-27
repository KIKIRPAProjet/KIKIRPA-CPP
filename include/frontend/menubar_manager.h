// include/frontend/menubar_manager.h
#pragma once

#include <QObject>

class QMenuBar;
class QAction;

class MenuBarManager : public QObject
{
    Q_OBJECT

public:
    explicit MenuBarManager(QObject *parent = nullptr);

    void setupMenus(QMenuBar *menuBar);
    // Reflète les ouvertures automatiques et la réduction du journal dans le menu.
    void setLogVisible(bool visible);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void importTilesRequested();
    void saveProjectRequested();
    void exportRequested();
    void homeRequested();
    void quitRequested();
    void toggleSideBar();
    void toggleLog(bool visible);
    void zoomInRequested();
    void zoomOutRequested();
    void fitToWindowRequested();
    void runRequested(int startStep);
    void comparisonRequested();
    void aboutRequested();
    void creditsRequested();

private:
    QAction *logAction = nullptr;
};
