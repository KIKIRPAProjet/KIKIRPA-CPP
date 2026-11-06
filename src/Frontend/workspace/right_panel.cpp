#include "Frontend/workspace/right_panel.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/workspace/workspace_center.h"
#include "Frontend/canvas/artworkcanvas.h"
#include "Frontend/workspace/grid_tab.h"
#include "Frontend/workspace/masks_tab.h"
#include "Frontend/workspace/assembly_tab.h"
#include "Frontend/workspace/alignment_tab.h"

namespace {
constexpr int kMinimumWidth = 218;
}

RightPanel::RightPanel(MainWindow *mainWindow, ProjectGrid *projectGrid,
                       WorkspaceCenter *centerArea, QWidget *parent)
    : QTabWidget(parent),
      m_mainWindow(mainWindow),
      m_projectGrid(projectGrid),
      m_centerArea(centerArea)
{
    setMinimumWidth(kMinimumWidth);

    ArtworkCanvas *canvas = m_centerArea->canvas();

    m_gridTab = new GridTab(m_mainWindow, m_projectGrid, canvas, this);
    m_alignmentTab = new AlignmentTab(m_mainWindow, canvas, this);

    addTab(m_gridTab, tr("Grille"));
    addTab(new MasksTab(m_mainWindow, canvas, this), tr("Calques"));
    addTab(new AssemblyTab(m_mainWindow, this), tr("Chaîne"));
    addTab(m_alignmentTab, tr("Infos"));
}

void RightPanel::updateGridState(const ProjectGrid::State &gridState)
{
    if (m_gridTab) {
        m_gridTab->updateState(gridState);
    }
}

void RightPanel::updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState)
{
    if (m_alignmentTab) {
        m_alignmentTab->updateInfo(projectName, gridState);
    }
}
