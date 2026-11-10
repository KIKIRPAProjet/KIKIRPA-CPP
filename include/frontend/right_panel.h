#ifndef RIGHT_PANEL_H
#define RIGHT_PANEL_H

#include <QTabWidget>
#include "projectgrid.h"
#include "image_layer.h"
class MainWindow;
class WorkspaceCenter;
class GridTab;
class AlignmentTab;
class MasksTab;
class RightPanel : public QTabWidget
{
    Q_OBJECT
public:
    explicit RightPanel(MainWindow *mainWindow, ProjectGrid *projectGrid,
                        WorkspaceCenter *centerArea, QWidget *parent = nullptr);

    GridTab *gridTab() const { return m_gridTab; }

    void updateGridState(const ProjectGrid::State &gridState);
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void setImageLayers(const QVector<ImageLayer> &layers);

private:
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    WorkspaceCenter *m_centerArea;

    GridTab *m_gridTab = nullptr;
    AlignmentTab *m_alignmentTab = nullptr;
    MasksTab *m_masksTab = nullptr;
};

#endif // RIGHT_PANEL_H