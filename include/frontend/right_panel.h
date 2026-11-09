#ifndef RIGHT_PANEL_H
#define RIGHT_PANEL_H

#include <QTabWidget>
#include "projectgrid.h"

class MainWindow;
class WorkspaceCenter;
class GridTab;
class AlignmentTab;

class RightPanel : public QTabWidget
{
    Q_OBJECT
public:
    explicit RightPanel(MainWindow *mainWindow, ProjectGrid *projectGrid,
                        WorkspaceCenter *centerArea, QWidget *parent = nullptr);

    GridTab *gridTab() const { return m_gridTab; }

    void updateGridState(const ProjectGrid::State &gridState);
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);

private:
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    WorkspaceCenter *m_centerArea;

    GridTab *m_gridTab = nullptr;
    AlignmentTab *m_alignmentTab = nullptr;
};

#endif // RIGHT_PANEL_H