#include "right_panel.h"
#include "mainwindow.h"
#include "workspace_center.h"
#include "artworkcanvas.h"
#include "grid_tab.h"
#include "masks_tab.h"
#include "assembly_tab.h"
#include "alignment_tab.h"

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
    m_masksTab = new MasksTab(m_mainWindow, canvas, this);
    addTab(m_masksTab, tr("Calques"));
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


void RightPanel::setImageLayers(const QVector<ImageLayer> &layers)
{
    if (m_masksTab) {
        m_masksTab->setImageLayers(layers);
    }
}
