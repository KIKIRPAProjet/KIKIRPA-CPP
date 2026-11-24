#pragma once

#include <QTabWidget>
#include "projectgrid.h"

class MainWindow;
class WorkspaceCenter;
class GridTab;
class AlignmentTab;
class AssemblyTab;

class RightPanel : public QTabWidget
{
    Q_OBJECT
public:
    explicit RightPanel(MainWindow *mainWindow, ProjectGrid *projectGrid,
                        WorkspaceCenter *centerArea, QWidget *parent = nullptr);

    GridTab *gridTab() const { return m_gridTab; }
    AlignmentTab *alignmentTab() const { return m_alignmentTab; }
    void showPairTab();

    void updateGridState(const ProjectGrid::State &gridState);
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void setActiveStep(int stepNumber);
    void showSavedState(int savedStepCount);

private:
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    WorkspaceCenter *m_centerArea;

    GridTab *m_gridTab = nullptr;
    AlignmentTab *m_alignmentTab = nullptr;
    AssemblyTab *m_assemblyTab = nullptr;
    int m_alignmentTabIndex = -1;   // index de l'onglet Paire (sa page est une zone de défilement)
};
