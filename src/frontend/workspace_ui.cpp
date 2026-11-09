/*
 * .cpp : le POURQUOI
 * - choix de conception : regrouper la construction des pages facilite la navigation entre les étapes ;
 * - cas limite : les contrôles doivent rester synchronisés avec l'état courant du projet ;
 * - unité ou repère : les pages suivent l'ordre des étapes du workflow frontend ;
 * - raison non évidente : bloquer temporairement certains signaux évite les mises à jour récursives.
 */
/*
 * Header : le QUOI
 * Gère les pages et les contrôles de l'espace de travail.
 * - ce que ça fait : construit les vues de travail et relie les contrôles aux données du projet ;
 * - ce que ça renvoie : aucune valeur directement, les changements sont transmis à l'interface ;
 * - quand ça lève une exception : si la création ou la configuration d'un contrôle échoue.
 */
//  gere les pages et les controles de espace de travail
#include "workspace_ui.h"
#include "artworkcanvas.h"
#include "homepage.h"
#include "mainwindow.h"
#include "projectgrid.h"
#include "simulationrunner.h"
#include "grid_tab.h"
#include "bottom_tabs.h"
#include "workspace_status_bar.h"
#include "workspace_center.h"
#include "left_toolbar.h"
#include "right_panel.h"
#include "top_toolbars.h"
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QChar>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QString>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <Qt>
#include <algorithm>

namespace {
constexpr int kSplitterHandleWidth = 5;
constexpr int kLeftToolbarWidth = 42;
constexpr int kCenterWidth = 760;
constexpr int kRightPanelWidth = 280;
constexpr int kBodyHeight = 560;
constexpr int kBottomTabsHeight = 165;
constexpr int kPaintToolFirst = 3;   // Pinceau
constexpr int kPaintToolLast = 6;    // Rectangle
constexpr int kMaskMode = 2;
}

WorkspaceUI::WorkspaceUI(MainWindow *mainWindow, HomePage *homePage,
                         ProjectGrid *projectGrid, SimulationRunner *simulationRunner)
    : QObject(mainWindow),
      m_mainWindow(mainWindow),
      m_projectGrid(projectGrid),
      m_simulationRunner(simulationRunner),
      m_homePage(homePage),
      m_pages(new QStackedWidget(mainWindow))
{
    m_pages->addWidget(m_homePage);
    createWorkspace();
    m_mainWindow->setCentralWidget(m_pages);
}

QWidget *WorkspaceUI::canvas() const
{
    return m_centerArea ? m_centerArea->canvas() : nullptr;
}

QWidget *WorkspaceUI::projectInfoPanel() const
{
    return m_centerArea ? m_centerArea->projectInfoPanel() : nullptr;
}

QLabel *WorkspaceUI::zoomLabel() const
{
    return m_centerArea ? m_centerArea->zoomLabel() : nullptr;
}

QTabWidget *WorkspaceUI::rightTabs() const { return m_rightPanel; }

QTabWidget *WorkspaceUI::bottomTabs() const { return m_bottomTabs; }

GridTab *WorkspaceUI::gridtab() const
{
    return m_rightPanel ? m_rightPanel->gridTab() : nullptr;
}

QWidget *WorkspaceUI::gridTabPage() const { return gridtab(); }

QComboBox *WorkspaceUI::captureOrderCombo() const
{
    return gridtab() ? gridtab()->captureOrderCombo() : nullptr;
}

QComboBox *WorkspaceUI::arrangementCombo() const
{
    return m_topToolbars ? m_topToolbars->arrangementCombo() : nullptr;
}

QLabel *WorkspaceUI::statusLabel() const
{
    return m_statusBar ? m_statusBar->statusLabel() : nullptr;
}

QLabel *WorkspaceUI::currentStepLabel() const
{
    return m_statusBar ? m_statusBar->currentStepLabel() : nullptr;
}

QProgressBar *WorkspaceUI::progressBar() const
{
    return m_statusBar ? m_statusBar->progressBar() : nullptr;
}

/**
 * Met à jour l'interface utilisateur en fonction de l'état de la grille du projet.
 * @param projectName Le nom du projet.
 * @param gridState L'état de la grille.
 */
void WorkspaceUI::updateFromGridState(const QString &projectName,
                                      const ProjectGrid::State &gridState)
{
    updateProjectInfo(projectName, gridState);

    if (m_centerArea && m_centerArea->canvas()) {
        ArtworkCanvas *canvasView = m_centerArea->canvas();
        canvasView->setImagePaths(gridState.tilePaths);
        canvasView->setGridState(gridState.gridRows, gridState.gridColumns,
                                 gridState.captureOrder, gridState.gridTiles,
                                 gridState.selectedGridCell);
        canvasView->setOverlap(gridState.horizontalOverlap, gridState.verticalOverlap);
    }

    if (m_rightPanel) {
        m_rightPanel->updateGridState(gridState);
    }

    m_mainWindow->setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}

/**
 * Met à jour les informations du projet dans l'interface utilisateur.
 * @param projectName Le nom du projet.
 * @param gridState L'état de la grille.
 */
void WorkspaceUI::updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState)
{
    if (m_centerArea) {
        m_centerArea->updateProjectInfo(projectName, gridState);
    }

    if (m_rightPanel) {
        m_rightPanel->updateProjectInfo(projectName, gridState);
    }

    if (m_topToolbars) {
        m_topToolbars->updateTitle(projectName, gridState.modality);
    }
}

/**
 * Crée l'espace de travail principal avec les barres d'outils, les onglets et les panneaux.
 */
void WorkspaceUI::createWorkspace()
{
    m_workspace = new QWidget;
    auto *root = new QVBoxLayout(m_workspace);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_topToolbars = new TopToolbars(m_mainWindow, m_projectGrid, m_simulationRunner, m_workspace);
    root->addWidget(m_topToolbars);

    auto *body = new QSplitter(Qt::Horizontal);
    body->setHandleWidth(kSplitterHandleWidth);
    body->setChildrenCollapsible(false);

    m_centerArea = new WorkspaceCenter(m_mainWindow);
    m_leftToolbar = new LeftToolbar;
    connect(m_leftToolbar, &LeftToolbar::toolSelected, this,
            [this](int index, const QString &name) {
        m_mainWindow->updateStatus(tr("Outil actif : %1").arg(name));
        if (m_mainWindow->currentMode == kMaskMode
                && index >= kPaintToolFirst && index <= kPaintToolLast) {
            m_centerArea->canvas()->toggleMask();
        }
    });
    m_rightPanel = new RightPanel(m_mainWindow, m_projectGrid, m_centerArea);

    body->addWidget(m_leftToolbar);
    body->addWidget(m_centerArea);
    body->addWidget(m_rightPanel);

    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    body->setStretchFactor(2, 0);
    body->setSizes({kLeftToolbarWidth, kCenterWidth, kRightPanelWidth});

    m_bottomTabs = new BottomTabs(m_mainWindow);

    auto *contentSplitter = new QSplitter(Qt::Vertical);
    contentSplitter->setHandleWidth(kSplitterHandleWidth);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->addWidget(body);
    contentSplitter->addWidget(m_bottomTabs);

    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setSizes({kBodyHeight, kBottomTabsHeight});

    root->addWidget(contentSplitter, 1);

    m_statusBar = new WorkspaceStatusBar(m_mainWindow);
    root->addWidget(m_statusBar);

    m_pages->addWidget(m_workspace);
}
