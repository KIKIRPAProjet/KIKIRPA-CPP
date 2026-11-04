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
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "homepage.h"
#include "mainwindow.h"
#include "projectgrid.h"
#include "simulationrunner.h"
#include "workspace_helpers.h"
#include "workspace_ui.h"
#include "grid_tab.h"
#include "alignment_tab.h"
#include "assembly_tab.h"
#include "masks_tab.h"


#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
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
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>


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

/**
 * Met à jour l'interface utilisateur en fonction de l'état de la grille du projet.
 * @param projectName Le nom du projet.
 * @param gridState L'état de la grille.
 */
void WorkspaceUI::updateFromGridState(const QString &projectName,
                                      const ProjectGrid::State &gridState)
{
   updateProjectInfo(projectName, gridState);

    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(m_canvas)) {
        canvasView->setImagePaths(gridState.tilePaths);
        canvasView->setGridState(gridState.gridRows, gridState.gridColumns,
                                 gridState.captureOrder, gridState.gridTiles,
                                 gridState.selectedGridCell);
    }

    if(m_gridTab){
        m_gridTab->updateState(gridState);
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
    m_projectNameEdit->setText(projectName);

    m_projectFolderLabel->setText(gridState.tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(gridState.tileDirectory));
    
    const QFileInfoList folderFiles = gridState.tileDirectory.isEmpty()
            ? QFileInfoList()
            : QDir(gridState.tileDirectory).entryInfoList(
                  QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        
    int validFiles = 0;
    for (const QFileInfo &file : folderFiles) {
        const QString suffix = file.suffix().toLower();
            if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
                ++validFiles;
        }   
    const bool allFilesValid = !folderFiles.isEmpty() && validFiles == folderFiles.size();
    
    m_projectFormatLabel->setText(allFilesValid
            ? tr("Format : tous les éléments sont au format TIFF compatible (%1 fichier(s)).")
                  .arg(validFiles)
            : tr("Format : %1 fichier(s) TIFF compatible(s) sur %2 ; certains éléments "
                "ne sont pas au bon format.")
                  .arg(validFiles).arg(folderFiles.size()));

    m_projectFormatLabel->setStyleSheet(allFilesValid
            ? QStringLiteral("color:#66c98a;")
            : QStringLiteral("color:#e0a45f;"));

    if (auto *title = m_workspace->findChild<QLabel *>(QStringLiteral("workspaceTitle"))) {
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage")
                           .arg(projectName, gridState.modality));
    }

    if(m_alignmentTab){
        m_alignmentTab->updateInfo(projectName,gridState);
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

    root->addWidget(createToolbar());
    root->addWidget(createModesBar());
    root->addWidget(createOptionsBar());

    auto *body = new QSplitter(Qt::Horizontal);
    body->setHandleWidth(5);
    body->setChildrenCollapsible(false);

    body->addWidget(createLeftToolbar());
    body->addWidget(createCenterArea());

    m_rightTabs = createRightTabs();
    body->addWidget(m_rightTabs);

    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    body->setStretchFactor(2, 0);
    body->setSizes({42, 760, 280});

    m_bottomTabs = createBottomTabs();

    auto *contentSplitter = new QSplitter(Qt::Vertical);
    contentSplitter->setHandleWidth(5);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->addWidget(body);
    contentSplitter->addWidget(m_bottomTabs);

    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setSizes({560, 165});
    
    root->addWidget(contentSplitter, 1);

    root->addWidget(createStatusBar());

    m_pages->addWidget(m_workspace);
}

void WorkspaceUI::createProjectMode(QWidget *center)
{
    m_projectInfoPanel = new QWidget(center);
    auto *projectInfoLayout = new QVBoxLayout(m_projectInfoPanel);
    projectInfoLayout->setContentsMargins(32, 32, 32, 32);
    projectInfoLayout->setSpacing(14);
    auto *projectInfoTitle = new QLabel(tr("Informations du projet"), m_projectInfoPanel);
    projectInfoTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
    projectInfoLayout->addWidget(projectInfoTitle);

    auto *projectNameForm = new QFormLayout;
    m_projectNameEdit = new QLineEdit(m_projectInfoPanel);
    m_projectNameEdit->setObjectName(QStringLiteral("projectNameEdit"));
    auto *renameProject = makeWorkspaceButton(tr("Renommer"), m_projectInfoPanel);
    auto *projectNameRow = new QHBoxLayout;
    projectNameRow->addWidget(m_projectNameEdit, 1);
    projectNameRow->addWidget(renameProject);
    projectNameForm->addRow(tr("Nom du projet"), projectNameRow);
    connect(renameProject, &QPushButton::clicked, this, [this]() {
        const QString newName = m_projectNameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(m_mainWindow, tr("Nom invalide"),
                tr("Le nom du projet ne peut pas être vide."));
            return;
        }
        m_mainWindow->projectName = newName;
        m_mainWindow->refreshProjectInfo();
        m_mainWindow->updateStatus(
            tr("Projet renommé : %1").arg(m_mainWindow->projectName));
    });
    projectInfoLayout->addLayout(projectNameForm);

    m_projectFolderLabel = makeMutedLabel(QString(), m_projectInfoPanel);
    m_projectFolderLabel->setWordWrap(true);
    projectInfoLayout->addWidget(m_projectFolderLabel);
    m_projectFormatLabel = new QLabel(m_projectInfoPanel);
    m_projectFormatLabel->setWordWrap(true);
    projectInfoLayout->addWidget(m_projectFormatLabel);
    projectInfoLayout->addStretch();
    static_cast<QVBoxLayout *>(center->layout())->addWidget(m_projectInfoPanel, 1);

}


/**
 * Crée la barre d'outils principale de l'espace de travail avec le titre et le bouton d'enregistrement.
 * @return Un QWidget contenant la barre d'outils.
 */
QWidget* WorkspaceUI::createToolbar()
{ 
    // creation du widget 
    QWidget *toolbar = new QWidget;
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);
    
    // creation du titre et du bouton enregistrer
    auto *title = new QLabel(tr("KIK-IRPA Assemblage"));
    title->setObjectName(QStringLiteral("workspaceTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();
    
    auto *save = makeWorkspaceButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, m_mainWindow, &MainWindow::saveProject);
    
    return toolbar;
}

/**
 * Crée la barre de modes de l'espace de travail avec les boutons pour chaque mode et les actions associées.
 * @return Un QWidget contenant la barre de modes.
 */
QWidget* WorkspaceUI::createModesBar()
{
    QWidget *stages = new QWidget;
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);
    for (int i = 0; i < workspaceModeNames().size(); ++i) {
        auto *button = makeWorkspaceButton(QStringLiteral("%1   %2").arg(i + 1).arg(workspaceModeNames().at(i)), stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { m_mainWindow->setMode(i); });
    }
    auto *run = makeWorkspaceButton(tr("▶ Tout lancer"), stages, true);
    stageLayout->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() { m_mainWindow->requestRun(1); });
    auto *compare = makeWorkspaceButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked,
            m_simulationRunner, &SimulationRunner::showComparison);
    
    return stages;
}

/**
 * Crée la barre d'options de l'espace de travail avec les contrôles pour l'arrangement des tuiles et les informations associées.
 * @return Un QWidget contenant la barre d'options.
 */
QWidget* WorkspaceUI::createOptionsBar()
{
    QWidget *modeOptions = new QWidget;
    auto *optionsLayout = new QHBoxLayout(modeOptions);
    optionsLayout->setContentsMargins(10, 3, 10, 3);
    
    auto *modeLabel = new QLabel(tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"));
    modeLabel->setObjectName(QStringLiteral("modeDescription"));
    optionsLayout->addWidget(modeLabel);
    optionsLayout->addStretch();
    
    m_arrangementCombo = new QComboBox;
    m_arrangementCombo->addItems({tr("Automatique (par lignes)"),
                                tr("Serpentin vertical, bas gauche"),
                                tr("Serpentin vertical, haut droite"),
                                tr("Serpentin horizontal"), tr("Par lignes"), tr("Par colonnes")});
    m_arrangementCombo->setObjectName(QStringLiteral("arrangementCombo"));
    optionsLayout->addWidget(m_arrangementCombo);
    connect(m_arrangementCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int value) {
        m_projectGrid->setCaptureOrder(value);
        m_mainWindow->updateStatus(tr("Ordre de prise de vue : %1").arg(m_arrangementCombo->currentText()));
    });

    return modeOptions;
}

/**
 * Crée la barre d'outils à gauche de l'espace de travail avec les boutons pour chaque outil et les actions associées. (pinceau, gomme, sélection, etc.)
 * @return Un QWidget contenant la barre d'outils.
 */
QWidget *WorkspaceUI::createLeftToolbar()
{
    QWidget *tools = new QWidget;
    tools->setFixedWidth(42);
    auto *toolLayout = new QVBoxLayout(tools);
    toolLayout->setContentsMargins(4, 5, 4, 5);
    
    const QStringList toolNames = {tr("Main (H)"), tr("Zoom (Z)"), tr("Sélection (V)"),
        tr("Pinceau (B)"), tr("Gomme (E)"), tr("Lasso (L)"), tr("Rectangle (M)"),
        tr("Point (P)"), tr("Mesure (I)")};
    
        for (int i = 0; i < toolNames.size(); ++i) {
        auto *tool = new QToolButton(tools);
        tool->setText(QStringList{QStringLiteral("✋"), QStringLiteral("⌕"), QStringLiteral("↖"),
                                  QStringLiteral("✎"), QStringLiteral("⌫"), QStringLiteral("⬠"),
                                  QStringLiteral("▭"), QStringLiteral("＋"), QStringLiteral("⌁")}.at(i));
        tool->setToolTip(toolNames.at(i));
        tool->setFixedSize(32, 32);
        toolLayout->addWidget(tool);
        connect(tool, &QToolButton::clicked, this, [this, toolNames, i]() {
            m_mainWindow->updateStatus(tr("Outil actif : %1").arg(toolNames.at(i)));
            if (m_mainWindow->currentMode == 2 && i >= 3 && i <= 6) {
                static_cast<ArtworkCanvas *>(m_canvas)->toggleMask();
            }
        });
    }
    toolLayout->addStretch();
    return tools;
}

/**
 * Crée la zone centrale de l'espace de travail avec le canevas d'artwork et les contrôles de zoom.
 * @return Un QWidget contenant la zone centrale.
 */
QWidget* WorkspaceUI::createCenterArea()
{
    QWidget *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    
    createProjectMode(center);
    
    m_canvas = new ArtworkCanvas(center);
    centerLayout->addWidget(m_canvas, 1);
    
    auto *canvasControls = new QWidget;
    canvasControls->setObjectName(QStringLiteral("canvasControls"));
    auto *canvasControlsLayout = new QHBoxLayout(canvasControls);
    
    canvasControlsLayout->setContentsMargins(8, 3, 8, 3);
    auto *minus = makeWorkspaceButton(QStringLiteral("−"), canvasControls);
    auto *plus = makeWorkspaceButton(QStringLiteral("+"), canvasControls);
    m_zoomLabel = new QLabel(QStringLiteral("12,4 %"));
    auto *fit = makeWorkspaceButton(tr("Ajuster"), canvasControls);
    auto *oneToOne = makeWorkspaceButton(QStringLiteral("100 %"), canvasControls);
    
    canvasControlsLayout->addWidget(minus);
    canvasControlsLayout->addWidget(plus);
    canvasControlsLayout->addWidget(m_zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    centerLayout->addWidget(canvasControls);

    m_projectInfoPanel->setVisible(false);

    connect(minus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMax(5, m_mainWindow->zoomPercent - 5);
        m_zoomLabel->setText(QString::number(m_mainWindow->zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(m_canvas)->setZoom(m_mainWindow->zoomPercent);
    });
    connect(plus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMin(1600, m_mainWindow->zoomPercent + 5);
        m_zoomLabel->setText(QString::number(m_mainWindow->zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(m_canvas)->setZoom(m_mainWindow->zoomPercent);
    });
    connect(fit, &QPushButton::clicked, this, [this]() {
        m_zoomLabel->setText(tr("Ajuster"));
        static_cast<ArtworkCanvas *>(m_canvas)->setZoom(100);
    });
    connect(oneToOne, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = 100;
        m_zoomLabel->setText(QStringLiteral("100 %"));
        static_cast<ArtworkCanvas *>(m_canvas)->setZoom(m_mainWindow->zoomPercent);
    });
    
    return center;
}

/**
 * Crée les onglets à droite de l'espace de travail avec les modes de calques, chaîne et informations.
 * @return Un QTabWidget contenant les onglets.
 */
QTabWidget *WorkspaceUI::createRightTabs()
{
    QTabWidget *rightTabs = new QTabWidget;
    rightTabs->setMinimumWidth(218);

    m_gridTab = new GridTab(m_mainWindow, m_projectGrid, static_cast<ArtworkCanvas*>(m_canvas));
    m_alignmentTab = new AlignmentTab(m_mainWindow, static_cast<ArtworkCanvas *>(m_canvas));
    
    rightTabs->addTab(m_gridTab, tr("Grille"));
    rightTabs->addTab(new MasksTab(m_mainWindow, static_cast<ArtworkCanvas*>(m_canvas)), tr("Calques"));
    rightTabs->addTab(new AssemblyTab(m_mainWindow), tr("Chaîne"));
    rightTabs->addTab(m_alignmentTab, tr("Infos"));
    
    return rightTabs;
}

/**
 * Crée les onglets en bas de l'espace de travail avec les paires, le journal et les positions.
 * @return Un QTabWidget contenant les onglets.
 */
QTabWidget *WorkspaceUI::createBottomTabs()
{
    QTabWidget *bottomTabs = new QTabWidget;
    bottomTabs->setMinimumHeight(80);
    m_pairsTable = new QTableWidget(4, 8, bottomTabs);
    m_pairsTable->setHorizontalHeaderLabels({tr("Paire"), tr("Sens"), tr("Statut"), tr("Confiance"),
        tr("Contrôle NCC"), tr("dx (px)"), tr("dy (px)"), tr("Erreur")});
    const QStringList pairRows[] = {
        {QStringLiteral("r00_c00 → r00_c01"), tr("horizontal"), tr("douteuse"), QStringLiteral("0,91"), QStringLiteral("0,37"), QStringLiteral("4 065,5"), QStringLiteral("-20,7"), QStringLiteral("3,95 px")},
        {QStringLiteral("r00_c00 → r01_c00"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,76"), QStringLiteral("23,5"), QStringLiteral("4 086,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r00_c01 → r01_c01"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,92"), QStringLiteral("23,8"), QStringLiteral("4 133,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r01_c00 → r01_c01"), tr("horizontal"), tr("fiable"), QStringLiteral("0,98"), QStringLiteral("0,65"), QStringLiteral("4 064,7"), QStringLiteral("29,9"), QStringLiteral("0,02 px")}
    };
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col)
            m_pairsTable->setItem(row, col, new QTableWidgetItem(pairRows[row].at(col)));
    }
    m_pairsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pairsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pairsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottomTabs->addTab(m_pairsTable, tr("Paires (4)"));
    
    QListWidget *journal = new QListWidget;
    journal->addItems({tr("14:12  Assemblage terminé — une paire à vérifier"),
                       tr("14:10  4 tuiles importées"),
                       tr("14:02  Projet ouvert")});
    bottomTabs->addTab(journal, tr("Journal"));
    auto *positions = new QTableWidget(4, 3);
    positions->setHorizontalHeaderLabels({tr("Tuile"), tr("X"), tr("Y")});
    for (int i = 0; i < 4; ++i) {
        positions->setItem(i, 0, new QTableWidgetItem(QStringLiteral("r%1_c%2").arg(i / 2).arg(i % 2)));
        positions->setItem(i, 1, new QTableWidgetItem(QString::number((i % 2) * 4065)));
        positions->setItem(i, 2, new QTableWidgetItem(QString::number((i / 2) * 4080)));
    }
    bottomTabs->addTab(positions, tr("Positions"));

    connect(m_pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { m_mainWindow->setMode(3); });
    connect(m_pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        m_mainWindow->updateStatus(tr("Paire sélectionnée : %1").arg(m_pairsTable->item(row, 0)->text()));
    });

    return bottomTabs;
}

/**
 * Crée la barre d'état de l'espace de travail avec les informations sur le zoom, le statut et la progression.
 * @return Un QWidget contenant la barre d'état.
 */
QWidget *WorkspaceUI::createStatusBar()
{
    QWidget *status = new QWidget;
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(10, 3, 10, 3);
    
    m_zoomLabel->setMinimumWidth(48);
    statusLayout->addWidget(makeMutedLabel(tr("x 1 812  y 2 310 px    Tuile r00_c01"), status));
    statusLayout->addStretch();
    m_statusLabel = new QLabel(tr("Affichage ajusté (contraste)"));
    statusLayout->addWidget(m_statusLabel);

    m_currentStepLabel = new QLabel(tr("Aucune étape"), m_mainWindow->statusBar());
    m_progressBar = new QProgressBar(m_mainWindow->statusBar());
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedWidth(160);

    m_mainWindow->statusBar()->addPermanentWidget(m_currentStepLabel);
    m_mainWindow->statusBar()->addPermanentWidget(m_progressBar);

    return status;
}
