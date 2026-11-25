#pragma once

#include <QTabWidget>
#include <QVector>
#include "projectgrid.h"
#include "image_layer.h"

class MainWindow;
class WorkspaceCenter;
class GridTab;
class AlignmentTab;
class MasksTab;
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
    void setImageLayers(const QVector<ImageLayer> &layers, const QRectF &bounds = {}) const;
    void setActiveStep(int stepNumber) const;

private:
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    WorkspaceCenter *m_centerArea;

    GridTab *m_gridTab = nullptr;
    AlignmentTab *m_alignmentTab = nullptr;
    MasksTab *m_masksTab = nullptr;
    AssemblyTab *m_assemblyTab = nullptr;
    int m_alignmentTabIndex = -1;   // index de l'onglet Paire (sa page est une zone de défilement)
};
