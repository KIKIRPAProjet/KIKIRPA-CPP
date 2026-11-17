#include "right_panel.h"
#include "mainwindow.h"
#include "workspace_center.h"
#include "artworkcanvas.h"
#include "grid_tab.h"
#include "masks_tab.h"
#include "assembly_tab.h"
#include "alignment_tab.h"

#include <QFrame>
#include <QScrollArea>

namespace {
constexpr int kMinimumWidth = 218;

// Place un onglet dans une zone de défilement, comme GridTab. Sans elle, la hauteur minimale d'un onglet chargé
// (l'onglet Paire demande environ 420 px) devient celle de toute la partie haute de l'espace de travail : sur un
// petit écran, le splitter vertical n'a plus de place pour le panneau du bas, qui ne peut plus être agrandi et
// paraît disparaître quand on le redimensionne.
QScrollArea *makeScrollable(QWidget *page, QWidget *parent)
{
    auto *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(page);
    return scrollArea;
}
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
    m_assemblyTab = new AssemblyTab(m_mainWindow, this);

    addTab(m_gridTab, tr("Grille"));   // GridTab est déjà une zone de défilement
    addTab(makeScrollable(m_assemblyTab, this), tr("Chaîne"));
    m_alignmentTabIndex = addTab(makeScrollable(m_alignmentTab, this), tr("Paire"));
    addTab(makeScrollable(new MasksTab(m_mainWindow, canvas, this), this), tr("Calques"));
}

//affichage de l'onglet pair
void RightPanel::showPairTab()
{
    if (m_alignmentTabIndex >= 0) {
        setCurrentIndex(m_alignmentTabIndex);   // la page de l'onglet est la zone de défilement qui contient m_alignmentTab
    }
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

void RightPanel::setActiveStep(int stepNumber)
{
    if (m_assemblyTab) {
        m_assemblyTab->setActiveStep(stepNumber);
    }
}
