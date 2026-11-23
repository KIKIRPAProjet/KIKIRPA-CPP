/*
 * .cpp : le POURQUOI
 * - choix de conception : la fenêtre principale coordonne les composants sans déplacer leur logique dédiée ;
 * - cas limite : les actions doivent rester cohérentes lorsque le projet ou la page active change ;
 * - unité ou repère : les index de page correspondent aux étapes affichées dans le workspace ;
 * - raison non évidente : les connexions Qt maintiennent l'interface synchronisée avec les signaux métier.
 */
/*
 * Header : le QUOI
 * Implémente la fenêtre principale de l'application.
 * - ce que ça fait : assemble les pages, les menus, les contrôles et les actions de l'application ;
 * - ce que ça renvoie : aucune valeur directement, les interactions sont transmises par des signaux ;
 * - quand ça lève une exception : si l'initialisation d'un composant de la fenêtre échoue.
 */
//  implemente la fenetre principale de lapplication

// src/frontend/mainwindow.cpp
#include "mainwindow.h"

#include "artworkcanvas.h"
#include "core/pipeline/pipeline_builder.h"
#include "dialog_helpers.h"
#include "frontendutils.h"
#include "homepage.h"
#include "left_toolbar.h"
#include "menubar_manager.h"
#include "project_serializer.h"
#include "project_setup_model.h"
#include "projectgrid.h"
#include "pipeline_runner.h"
#include "workspace_ui.h"
#include "core/io/unicode_path.h"
#include "alignment_tab.h"
#include "bottom_tabs.h"
#include "right_panel.h"
#include "top_toolbars.h"
#include "workspace_status_bar.h"

#include <nlohmann/json.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QtMath>
#include <QProgressDialog>

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <wincodec.h>
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("KIK-IRPA Assemblage"));
    resize(1228, 780);
    setMinimumSize(920, 600);
    applyStyleSheet();

    projectGrid = new ProjectGrid(this);
    pipelineRunner = new PipelineRunnerClass(this);
    workspaceUI = new WorkspaceUI(this, new HomePage, projectGrid, pipelineRunner);
    menuBarManager = new MenuBarManager(this);
    menuBarManager->setupMenus(menuBar());
    refreshGrid();
    refreshProjectInfo();
    setupProjectGridConnections();
    setupPipelineConnections();
    setupHomePageConnections();
    setupMenuConnections();
    setupPageConnections();
    setupPairAlignmentConnections();

    if (auto *canvasView = qobject_cast<ArtworkCanvas *>(workspaceUI->canvas())) {
        connect(canvasView, &ArtworkCanvas::importTilesRequested,
                this, &MainWindow::chooseTilesDirectory);
    }

    auto *nextDubiousShortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    connect(nextDubiousShortcut, &QShortcut::activated,
            this, &MainWindow::selectNextDubiousPair);
}

void MainWindow::applyStyleSheet()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget { background:#303030; color:#d5d5d5; font-size:12px; }"
        "QMenuBar,QMenu { background:#242424; color:#dedede; }"
        "QMenuBar::item:selected,QMenu::item:selected { background:#3d6e9d; }"
        "QToolBar { background:#2c2c2c; border:0; spacing:4px; }"
        "QPushButton,QToolButton,QComboBox,QSpinBox,QLineEdit { background:#3b3b3b;"
        " color:#dedede; border:1px solid #4a4a4a; border-radius:3px; padding:5px 8px; }"
        "QPushButton:hover,QToolButton:hover { background:#4a4a4a; }"
        "QPushButton[primary=\"true\"] { background:#397fb8; border-color:#438bca; color:white; }"
        "QPushButton[primary=\"true\"]:hover { background:#4b91cd; }"
        "QPushButton:checked,QToolButton:checked { background:#2c5273; border-color:#438bca; color:#ffffff; }"
        "QPushButton:disabled,QToolButton:disabled { background:#2b2b2b; border-color:#3a3a3a; color:#666666; }"
        "QTabWidget::pane { border:1px solid #424242; }"
        "QTabBar::tab { background:#303030; color:#aaa; padding:7px 12px; }"
        "QTabBar::tab:selected { background:#3b3b3b; color:white; border-bottom:2px solid #438bca; }"
        // Flèches de défilement des onglets (panneau étroit) : sans ces règles, le padding des QToolButton ne laisse
        // plus de place pour dessiner la flèche.
        "QTabBar::scroller { width:40px; }"
        "QTabBar QToolButton { padding:0px; border-radius:0px; }"
        "QTableWidget,QListWidget { background:#303030; alternate-background-color:#363636;"
        " gridline-color:#484848; selection-background-color:#36536d; }"
        "QHeaderView::section { background:#303030; color:#bdbdbd; border:0;"
        " border-bottom:1px solid #505050; padding:5px; }"
        "QProgressBar { background:#242424; border:0; border-radius:4px; height:8px; }"
        "QProgressBar::chunk { background:#43a56c; border-radius:4px; }"
        "QSplitter::handle { background:#202020; }"
        "QSplitter::handle:hover { background:#438bca; }"
        "QLabel[muted=\"true\"] { color:#999; }"
        "QCheckBox { spacing:7px; }"
        // Un élément désactivé doit se voir : texte et bordure atténués, entrées de menu grisées.
        "QWidget:disabled { color:#6f6f6f; }"
        "QPushButton:disabled,QToolButton:disabled,QComboBox:disabled,QSpinBox:disabled,QLineEdit:disabled {"
        " background:#333; border-color:#3d3d3d; }"
        "QComboBox QAbstractItemView::item:disabled { color:#6f6f6f; }"));
}

void MainWindow::setupProjectGridConnections()
{
    connect(projectGrid, &ProjectGrid::gridUpdated,
            this, &MainWindow::refreshProjectInfo);
    connect(projectGrid, &ProjectGrid::gridValidated,
            this, &MainWindow::refreshProjectInfo);
    connect(projectGrid, &ProjectGrid::referenceImageLoaded,
            this, &MainWindow::refreshProjectInfo);
    connect(projectGrid, &ProjectGrid::messageRequested,
            this, &MainWindow::showGridMessage);
    connect(projectGrid, &ProjectGrid::statusRequested,
            this, &MainWindow::updateStatus);
    connect(projectGrid, &ProjectGrid::logRequested,
            workspaceUI, &WorkspaceUI::appendLog);
    connect(projectGrid, &ProjectGrid::tilesImported,
            this, &MainWindow::handleTilesImported);
    connect(projectGrid, &ProjectGrid::referenceImageImportRequested, this, [this]() {
        if (chooseReferenceImage())
            projectGrid->compareReferenceImage();
    });
    connect(projectGrid, &ProjectGrid::comparisonReady,
            this, &MainWindow::showReferenceComparison);
    connect(projectGrid, &ProjectGrid::gridValidationCompleted,
            this, &MainWindow::handleGridValidation);
}

void MainWindow::setupPipelineConnections()
{
    connect(pipelineRunner, &PipelineRunnerClass::mosaicFileReady,
        this, [this](const QString& imagePath) {
            mosaicFilePath = imagePath;
        });

    // Au lancement du calcul, la préparation des vignettes s'arrête après son image en cours : l'import du backend
    // crée les pyramides manquantes, sans que deux images pleine résolution soient lues en même temps.
    connect(pipelineRunner, &PipelineRunnerClass::runStarted,
            this, [this]() {
                mosaicFilePath.clear();
                projectGrid->cancelBackgroundPyramidGeneration();
            });
    connect(pipelineRunner, &PipelineRunnerClass::runStarted, this, [this]() {
        workspaceUI->progressBar()->setValue(0);
        if (workspaceUI->bottomTabs())
            workspaceUI->bottomTabs()->setVisible(true);
        panelsVisible = true;
    });
    connect(pipelineRunner, &PipelineRunnerClass::progressAdvanced,
            workspaceUI->progressBar(), &QProgressBar::setValue);
    connect(pipelineRunner, &PipelineRunnerClass::stepChanged,
            this, [this](int stepNumber, const QString &stepName) {
                workspaceUI->currentStepLabel()->setText(stepName);
                workspaceUI->setActiveStep(stepNumber);
    });
    connect(pipelineRunner, &PipelineRunnerClass::statusChanged,
            this, &MainWindow::updateStatus);
    connect(pipelineRunner, &PipelineRunnerClass::cancellationAvailable,
            this, &MainWindow::showCancelRunButton);
    connect(pipelineRunner, &PipelineRunnerClass::runFinished,
            this, &MainWindow::removeCancelRunButton);
    connect(pipelineRunner, &PipelineRunnerClass::runCancelled,
            this, &MainWindow::removeCancelRunButton);
    connect(pipelineRunner, &PipelineRunnerClass::comparisonRequested,
            this, &MainWindow::showComparisonDialog);
    connect(pipelineRunner, &PipelineRunnerClass::comparisonVisibilityChanged,
            this, [this](bool visible) {
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setComparisonVisible(visible);
    });
    connect(pipelineRunner, &PipelineRunnerClass::logMessage,
            workspaceUI, &WorkspaceUI::appendLog);
    connect(pipelineRunner, &PipelineRunnerClass::mosaicReady,
            this, [this](const QImage &image) {
                if (auto *canvas = dynamic_cast<ArtworkCanvas*>(workspaceUI->canvas())) {
                    canvas->setMosaicPreview(image);
                    setMode(5); // 5 = Mode "Export", forcera l'affichage de l'image
                    updateStatus(tr("Mosaïque finale générée."));
                    QTimer::singleShot(0, this, &MainWindow::showExportDialog);
                }
    });
}

void MainWindow::setupHomePageConnections()
{
    connect(workspaceUI->homePage(), &HomePage::projectCreationRequested,
            this, &MainWindow::initializeNewProject);
    connect(workspaceUI->homePage(), &HomePage::projectLoaded,
            this, &MainWindow::applyLoadedProject);
    connect(workspaceUI->homePage(), &HomePage::currentProjectRequested,
            this, &MainWindow::openCurrentProjectFromHome);
}

void MainWindow::setupMenuConnections()
{
    connect(menuBarManager, &MenuBarManager::newProjectRequested,
            workspaceUI->homePage(), &HomePage::createProject);
    connect(menuBarManager, &MenuBarManager::openProjectRequested,
            workspaceUI->homePage(), &HomePage::openProject);
    connect(menuBarManager, &MenuBarManager::importTilesRequested,
            this, &MainWindow::chooseTilesDirectory);
    connect(menuBarManager, &MenuBarManager::saveProjectRequested,
            this, &MainWindow::saveProject);
    connect(menuBarManager, &MenuBarManager::exportRequested,
            this, &MainWindow::showExportDialog);
    connect(menuBarManager, &MenuBarManager::homeRequested, this, [this]() {
        workspaceUI->pages()->setCurrentIndex(0);
    });
    connect(menuBarManager, &MenuBarManager::quitRequested,
            qApp, &QApplication::quit);
    connect(menuBarManager, &MenuBarManager::undoRequested,
            this, [this]() { updateStatus(tr("Dernière action annulée.")); });
    connect(menuBarManager, &MenuBarManager::redoRequested,
            this, [this]() { updateStatus(tr("Action rétablie.")); });
    connect(menuBarManager, &MenuBarManager::togglePanelsRequested, this, [this]() {
        panelsVisible = !panelsVisible;
        workspaceUI->rightTabs()->setVisible(panelsVisible);
        workspaceUI->bottomTabs()->setVisible(panelsVisible);
    });
    connect(menuBarManager, &MenuBarManager::zoomInRequested, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 10);
        workspaceUI->zoomLabel()->setText(QStringLiteral("%1 %").arg(zoomPercent));
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setZoom(zoomPercent);
    });
    connect(menuBarManager, &MenuBarManager::zoomOutRequested, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 10);
        workspaceUI->zoomLabel()->setText(QStringLiteral("%1 %").arg(zoomPercent));
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setZoom(zoomPercent);
    });
    connect(menuBarManager, &MenuBarManager::fitToWindowRequested, this, [this]() {
        zoomPercent = 100;
        workspaceUI->zoomLabel()->setText(QStringLiteral("100 %"));
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->resetView();
    });
    connect(menuBarManager, &MenuBarManager::modeChangeRequested, this, [this](int mode) {
        setMode(mode);
        if (mode == 1) {
            updateStatus(tr("Grille réorganisée selon l'ordre sélectionné."));
        } else if (mode == 2) {
            static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setMaskVisible(true);
            updateStatus(tr("Proposition de masque affichée à valider par l'opérateur."));
        }
    });
    connect(menuBarManager, &MenuBarManager::runRequested,
            this, &MainWindow::requestRun);
    connect(menuBarManager, &MenuBarManager::comparisonRequested,
            pipelineRunner, &PipelineRunnerClass::showComparison);
    connect(menuBarManager, &MenuBarManager::aboutRequested, this, [this]() {
        QMessageBox::about(this, tr("À propos"),
            tr("KIK-IRPA Assemblage : prototype d'interface.\n"
               "Les traitements d'image nécessitent le backend, qui n'est pas encore connecté."));
    });
}

void MainWindow::setupPageConnections()
{
    connect(workspaceUI->pages(), &QStackedWidget::currentChanged, this, [this](int index) {
        menuBar()->setVisible(index != 0);
        statusBar()->setVisible(index != 0);
    });
    menuBar()->setVisible(false);
    statusBar()->setVisible(false);
}

void MainWindow::setupPairAlignmentConnections()
{
    m_pairs.clear();
    m_currentPairIndex = 0;

    /* À décommenter lors de la connexion au backend (remplace defaultPairAlignments() après le run) ---
    // Lorsque le DataStoreClass du pipeline contient la clé "matches" :
    // m_pairs.clear();
    // const auto &backendPairs = dataStore.get<std::vector<DataNs::PairMatches>>("matches");
    // for (int i = 0; i < static_cast<int>(backendPairs.size()); ++i) {
    //     m_pairs.append(FrontendUtils::pairInfoFromBackend(
    //         i, tileNameA, tileNameB, backendPairs[i]));
    // }
    */

    if (RightPanel *right = workspaceUI->rightPanel()) {
        if (AlignmentTab *alignTab = right->alignmentTab()) {
            connect(alignTab, &AlignmentTab::pairSelectionRequested,
                    this, &MainWindow::selectPair);
            connect(alignTab, &AlignmentTab::nextDubiousRequested,
                    this, &MainWindow::selectNextDubiousPair);
            connect(alignTab, &AlignmentTab::pairValidated,
                    this, &MainWindow::validatePair);
            connect(alignTab, &AlignmentTab::manualOffsetApplied,
                    this, &MainWindow::applyManualPairOffset);
        }
    }

    if (TopToolbars *top = workspaceUI->topToolbars()) {
        connect(top, &TopToolbars::previousPairRequested,
                this, &MainWindow::selectPreviousPair);
        connect(top, &TopToolbars::nextDubiousPairRequested,
                this, &MainWindow::selectNextDubiousPair);
        connect(top, &TopToolbars::togglePanelsRequested, this, [this]() {
            panelsVisible = !panelsVisible;
            workspaceUI->rightTabs()->setVisible(panelsVisible);
            workspaceUI->bottomTabs()->setVisible(panelsVisible);
        });
    }

    refreshPairViews();
}

//permet la sélection d'une nouvelles paires et la mise à jour des affichages
void MainWindow::selectPair(int pairIndex)
{
    if (m_pairs.isEmpty())
        return;
    m_currentPairIndex = qBound(0, pairIndex, m_pairs.size() - 1);
    refreshPairViews();

    if (currentMode == 3 && workspaceUI->rightPanel()) {
        workspaceUI->rightPanel()->showPairTab();
    }

    const auto &pair = m_pairs.at(m_currentPairIndex);
    updateStatus(tr("Paire sélectionnée : %1 (%2)")
                     .arg(pair.pairLabel(),
                          pair.isDubious() ? tr("douteuse : %1").arg(pair.statusReason) : tr("fiable")));
}

void MainWindow::selectPreviousPair()
{
    if (m_pairs.isEmpty())
        return;
    setMode(3);
    const int prevIndex = (m_currentPairIndex - 1 + m_pairs.size()) % m_pairs.size();
    selectPair(prevIndex);
}

//
void MainWindow::selectNextDubiousPair()
{
    if (m_pairs.isEmpty())
        return;
    setMode(3);
    for (int offset = 1; offset <= m_pairs.size(); ++offset) {
        const int idx = (m_currentPairIndex + offset) % m_pairs.size();
        if (m_pairs.at(idx).isDubious()) {
            selectPair(idx);
            return;
        }
    }
    updateStatus(tr("Aucune paire douteuse restante à inspecter."));
}

//une fois une paire validée, mise à jour du status
void MainWindow::validatePair(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= m_pairs.size())
        return;
    auto &pair = m_pairs[pairIndex];
    pair.status = FrontendUtils::PairStatus::Validated;
    pair.intervention = tr("validée par l'opérateur");
    refreshPairViews();
    updateStatus(tr("Paire %1 validée par l'opérateur ; les étapes suivantes sont à refaire.")
                     .arg(pair.pairLabel()));
}

//met à jours les variables après modification manuelle
void MainWindow::applyManualPairOffset(int pairIndex, double dx, double dy)
{
    if (pairIndex < 0 || pairIndex >= m_pairs.size())
        return;
    auto &pair = m_pairs[pairIndex];
    pair.dx = dx;
    pair.dy = dy;
    pair.intervention = tr("manuelle (%1 ; %2 px)")
                            .arg(QString::number(dx, 'f', 1).replace('.', ','),
                                 QString::number(dy, 'f', 1).replace('.', ','));
    refreshPairViews();
}

//comptage du nombre de paire douteuse et en échec, mise à jour des tables 
void MainWindow::refreshPairViews()
{
    if (m_pairs.isEmpty())
        return;

    m_currentPairIndex = qBound(0, m_currentPairIndex, m_pairs.size() - 1);
    const auto &currentPair = m_pairs.at(m_currentPairIndex);

    int dubiousCount = 0;
    int failedCount = 0;
    for (const auto &p : m_pairs) {
        if (p.status == FrontendUtils::PairStatus::Dubious)
            ++dubiousCount;
        else if (p.status == FrontendUtils::PairStatus::Failed)
            ++failedCount;
    }

    if (RightPanel *right = workspaceUI->rightPanel()) {
        if (AlignmentTab *alignTab = right->alignmentTab()) {
            alignTab->setPairs(m_pairs, m_currentPairIndex);
        }
    }

    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(workspaceUI->canvas())) {
        canvasView->setCurrentPair(currentPair);
    }

    if (TopToolbars *top = workspaceUI->topToolbars()) {
        top->updateDubiousState(dubiousCount);
    }

    if (WorkspaceStatusBar *status = workspaceUI->workspaceStatusBar()) {
        status->updatePairSummary(m_currentPairIndex + 1, m_pairs.size(), dubiousCount, failedCount);
    }

    if (currentMode == 3) {
        if (auto *label = workspaceUI->workspace()->findChild<QLabel *>(QStringLiteral("modeDescription"))) {
            label->setText(tr("Vue Paire : %1 (gauche) et %2 (droite) · zoom synchronisé · zone orange = recouvrement")
                               .arg(currentPair.tileA, currentPair.tileB));
        }
    }
}

void MainWindow::initializeNewProject(const QString &name, const QString &selectedTileDirectory,
                                      const QString &selectedOutputDirectory,
                                      const QStringList &newTilePaths)
{
    mosaicFilePath.clear();
    projectName = name;
    outputDirectory = selectedOutputDirectory;
    projectPath.clear();
    projectCreated = true;
    projectGrid->createNewProject(selectedTileDirectory, newTilePaths);

    workspaceUI->pages()->setCurrentWidget(workspaceUI->workspace());
    setMode(0);
    refreshProjectInfo();
    const int tileCount = projectGrid->state().tilePaths.size();
    workspaceUI->homePage()->setCurrentProjectInfo(
        projectCreated, projectPath, projectName, tileCount);
    if (tileCount == 0) {
        updateStatus(tr("Projet créé. Aucun fichier image reconnu dans le dossier des tuiles."));
    } else {
        updateStatus(tr("Projet créé avec %1 tuile(s) trouvée(s).").arg(tileCount));
    }
}

void MainWindow::applyLoadedProject(const QString &path)
{
    if (pipelineRunner->isRunning()) {
        updateStatus(tr("Attendez la fin ou la mise en pause du calcul avant d'ouvrir un projet."));
        return;
    }
    ProjectData projectData;
    QString errorMessage;
    if (!ProjectSerializer::loadFromFile(path, &projectData, &errorMessage)) {
        QMessageBox::warning(this, tr("Ouverture impossible"), errorMessage);
        return;
    }
    const ProjectResumeSummary summary = ProjectSerializer::summarize(projectData);
    QMessageBox opening(QMessageBox::Information, tr("Ouvrir le projet"), summary.text(),
                        QMessageBox::Open | QMessageBox::Cancel, this);
    auto *resumeButton = opening.addButton(tr("Ouvrir et reprendre"), QMessageBox::AcceptRole);
    resumeButton->setEnabled(summary.canResume);
    opening.exec();
    if (opening.clickedButton() == opening.button(QMessageBox::Cancel) || !opening.clickedButton())
        return;
    const bool resume = opening.clickedButton() == resumeButton;
    mosaicFilePath.clear();
    projectName = projectData.projectName;
    outputDirectory = projectData.outputDirectory;
    const bool hasSavedGrid = !projectData.gridState.gridTiles.isEmpty();
    projectGrid->setState(projectData.gridState);
    if (!hasSavedGrid)
        projectGrid->initializeGridFromPaths();
    projectPath = path;
    projectCreated = true;
    refreshProjectInfo();
    workspaceUI->pages()->setCurrentWidget(workspaceUI->workspace());
    setMode(projectData.mode);
    workspaceUI->homePage()->setCurrentProjectInfo(
        projectCreated, projectPath, projectName, projectGrid->state().tilePaths.size());
    workspaceUI->homePage()->rememberRecentProject(path);
    updateStatus(tr("Projet ouvert sans recalcul. Vérifiez les chemins des tuiles."));
    if (resume) {
        pipelineRunner->setScriptJson(QString::fromStdString(projectData.backendScript.dump()));
        pipelineRunner->setStartFromStepName(QString());
        pipelineRunner->startRun(1, true);
    }
}

void MainWindow::openCurrentProjectFromHome()
{
    workspaceUI->pages()->setCurrentWidget(workspaceUI->workspace());
    setMode(1);
    updateStatus(tr("Projet courant : %1").arg(projectName));
}

void MainWindow::setMode(int mode)
{
    currentMode = qBound(0, mode, 5);
    workspaceUI->pages()->setCurrentWidget(workspaceUI->workspace());
    if (QTabWidget *tabs = workspaceUI->rightTabs()) {
        const bool isGridMode = (currentMode == 1);
        const bool isPairMode = (currentMode == 3);
        const bool isLayersMode = (currentMode != 1);

        if (tabs->count() >= 4) {
            tabs->setTabEnabled(0, isGridMode);
            tabs->setTabToolTip(0, isGridMode ? QString() : tr("Disponible uniquement en mode Tuiles"));

            tabs->setTabEnabled(1, true); // Chaîne toujours accessible

            tabs->setTabEnabled(2, isPairMode);
            tabs->setTabToolTip(2, isPairMode ? QString() : tr("Disponible uniquement en mode Alignement"));

            tabs->setTabEnabled(3, isLayersMode);
            tabs->setTabToolTip(3, isLayersMode ? QString() : tr("Indisponible en mode Tuiles"));
        }

        if (isGridMode) {
            tabs->setCurrentIndex(0);
        } else if (currentMode == 2) {
            tabs->setCurrentIndex(3);
        } else if (isPairMode) {
            tabs->setCurrentIndex(2);
        } else if (tabs->currentIndex() >= 0 && !tabs->isTabEnabled(tabs->currentIndex())) {
            tabs->setCurrentIndex(1); // Chaîne par défaut
        }
    }
    if (auto *button = workspaceUI->workspace()->findChild<QPushButton *>(QStringLiteral("mode%1").arg(currentMode)))
        button->setChecked(true);

    const QStringList descriptions = {
        tr("Projet : aperçu général"), tr("Vue Grille de tuiles : organiser et vérifier l'import"),
        tr("Tuile r00_c00 : rouge : bords sombres proposés · bleu : lasso en cours"),
        tr("Vue Paire : points de contrôle et correction"), tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"),
        tr("Export : TIFF 16 bits, profil ICC conservé")
    };
    if (auto *label = workspaceUI->workspace()->findChild<QLabel *>(QStringLiteral("modeDescription")))
        label->setText(descriptions.at(currentMode));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(workspaceUI->canvas()))
        canvasView->setMode(currentMode);
    if (workspaceUI->projectInfoPanel())
        workspaceUI->projectInfoPanel()->setVisible(currentMode == 0);
    if (workspaceUI->canvas())
        workspaceUI->canvas()->setVisible(currentMode != 0);
    if (workspaceUI->leftToolbar())
        workspaceUI->leftToolbar()->setVisible(currentMode == 2 || currentMode == 3);
    if (auto *canvasControls = workspaceUI->workspace()->findChild<QWidget *>(
                QStringLiteral("canvasControls"))) {
        canvasControls->setVisible(currentMode != 0);
    }
    const bool isPairMode = (currentMode == 3);
    if (auto *overlayBtn = workspaceUI->workspace()->findChild<QWidget *>(
                QStringLiteral("pairOverlayButton"))) {
        overlayBtn->setVisible(isPairMode);
    }
    if (auto *prevPairBtn = workspaceUI->workspace()->findChild<QWidget *>(
                QStringLiteral("prevPairButton"))) {
        prevPairBtn->setVisible(isPairMode);
    }
    if (auto *nextDubiousBtn = workspaceUI->workspace()->findChild<QWidget *>(
                QStringLiteral("nextDubiousButton"))) {
        nextDubiousBtn->setVisible(isPairMode);
    }
    workspaceUI->arrangementCombo()->setVisible(currentMode == 1);
    if (currentMode == 1)
        workspaceUI->bottomTabs()->setCurrentIndex(0);
    if (currentMode == 3) {
        if (workspaceUI->bottomTabs())
            workspaceUI->bottomTabs()->setVisible(true);
        panelsVisible = true;
        workspaceUI->bottomTabs()->setCurrentIndex(1);
        if (workspaceUI->rightPanel())
            workspaceUI->rightPanel()->showPairTab();
        refreshPairViews();
    }
    updateStatus(tr("Mode : %1").arg(FrontendUtils::MODE_NAMES.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    workspaceUI->updateFromGridState(projectName, projectGrid->state());

    const bool hasTiles = !projectGrid->state().tilePaths.isEmpty();
    const QString lockedTip = hasTiles ? QString() : tr("Importez d'abord un dossier de tuiles");
    for (int m = 2; m <= 5; ++m) {
        if (auto *btn = workspaceUI->workspace()->findChild<QPushButton *>(QStringLiteral("mode%1").arg(m))) {
            btn->setEnabled(hasTiles);
            btn->setToolTip(lockedTip);
        }
    }
    if (auto *runBtn = workspaceUI->workspace()->findChild<QPushButton *>(QStringLiteral("runPipelineButton"))) {
        runBtn->setEnabled(hasTiles);
        runBtn->setToolTip(lockedTip);
    }
}

void MainWindow::refreshGrid()
{
    workspaceUI->updateFromGridState(projectName, projectGrid->state());
}

void MainWindow::refreshGridControls()
{
    workspaceUI->updateFromGridState(projectName, projectGrid->state());
}

void MainWindow::chooseTilesDirectory()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, tr("Sélectionner un dossier de photos"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!directoryPath.isEmpty())
        projectGrid->importTiles(directoryPath);
}

bool MainWindow::chooseReferenceImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer une image de référence"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;Tous les fichiers (*)"));
    if (!path.isEmpty())
        projectGrid->importReferenceImage(path);
    return !projectGrid->state().referenceImagePath.isEmpty();
}

void MainWindow::showReferenceComparison(const QString &referencePath, const QString &tilePath,
                                         const QImage &referenceImage,
                                         const QImage &tileImage)
{
    DialogHelpers::showReferenceComparisonDialog(
        this, referencePath, tilePath, referenceImage, tileImage);
    updateStatus(tr("Comparaison visuelle terminée."));
}

void MainWindow::showGridMessage(const QString &title, const QString &message, bool warning)
{
    if (warning)
        QMessageBox::warning(this, title, message);
    else
        QMessageBox::information(this, title, message);
}

void MainWindow::handleGridValidation(bool valid, int activeTileCount,
                                      const QStringList &errors)
{
    const ProjectGrid::State &gridState = projectGrid->state();
    if (!valid) {
        QMessageBox::warning(this, tr("Grille à corriger"), errors.join(QLatin1Char('\n')));
        updateStatus(tr("La grille n'est pas valide : %1 problème(s).").arg(errors.size()));
        return;
    }

    if (outputDirectory.isEmpty()) {
        outputDirectory = QDir(gridState.tileDirectory).filePath(QStringLiteral("output"));
    }

    FrontendNs::ProjectSetup setup = buildCurrentProjectSetup();

    std::filesystem::path scriptPath;
    try {
        FrontendNs::ProjectSetupModelClass model;
        model.setSetup(std::move(setup));
        scriptPath = model.writeBackendScript();
    } catch (const std::exception &exception) {
        QMessageBox::warning(this, tr("JSON non généré"),
            tr("La grille est valide, mais le JSON backend n'a pas pu être généré :\n%1")
                .arg(QString::fromUtf8(exception.what())));
        updateStatus(tr("La génération du JSON backend a échoué."));
        return;
    }

    updateStatus(tr("Grille validée : %1 tuile(s), %2 × %3, ordre « %4 ».")
                     .arg(activeTileCount).arg(gridState.gridRows).arg(gridState.gridColumns)
                     .arg(workspaceUI->captureOrderCombo() ? workspaceUI->captureOrderCombo()->currentText() : QString()));
    QMessageBox::information(this, tr("Grille validée"),
        tr("La grille et ses paramètres sont valides.\n\nJSON backend généré :\n%1")
            .arg(QString::fromStdString(IONs::pathToUtf8(scriptPath))));
}

void MainWindow::handleTilesImported(const QString &directory, int importedCount)
{
    setMode(1);
    const QString msg = tr("%1 photo(s) importée(s) depuis « %2 ».")
        .arg(importedCount).arg(QFileInfo(directory).fileName());
    updateStatus(msg);
    workspaceUI->appendLog(msg);
}

void MainWindow::saveProject()
{
    const ProjectGrid::State &gridState = projectGrid->state();
    QString path = projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"),
            projectName + QStringLiteral(".kikproj"), tr("Projet KIK-IRPA (*.kikproj);;JSON (*.json)"));
    if (path.isEmpty())
        return;
    QString errorMessage;
    nlohmann::json backendScript;
    try {
        backendScript = nlohmann::json::parse(generatePipelineScript(1).toStdString());
    } catch (const std::exception &exception) {
        // Un projet en cours de configuration reste enregistrable sans script exécutable.
        workspaceUI->appendLog(tr("Projet enregistré sans script exécutable : %1")
                                   .arg(QString::fromUtf8(exception.what())));
    }
    if (!ProjectSerializer::saveToFile(
            path, projectName, outputDirectory, currentMode, gridState, &errorMessage, backendScript)) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
            tr("Le projet n'a pas pu être enregistré : %1").arg(errorMessage));
        return;
    }
    projectPath = path;
    projectCreated = true;
    workspaceUI->homePage()->setCurrentProjectInfo(
        projectCreated, projectPath, projectName, gridState.tilePaths.size());
    workspaceUI->homePage()->rememberRecentProject(path);
    updateStatus(tr("Projet enregistré : %1").arg(QFileInfo(path).fileName()));
}

FrontendNs::ProjectSetup MainWindow::buildCurrentProjectSetup() const
{
    const auto& gridState = projectGrid->state();
    FrontendNs::ProjectSetup setup;
    setup.projectName = projectName.toStdString();
    setup.inputDirectory = gridState.tileDirectory.isEmpty()
        ? std::filesystem::current_path()
        : IONs::pathFromUtf8(gridState.tileDirectory.toStdString());
    setup.outputDirectory = outputDirectory.isEmpty()
        ? (setup.inputDirectory / "output")
        : IONs::pathFromUtf8(outputDirectory.toStdString());
    setup.gridRows = gridState.gridRows;
    setup.gridColumns = gridState.gridColumns;
    setup.overlap = qBound(0.01, static_cast<double>(gridState.horizontalOverlap) / 100.0, 0.99);
    setup.verticalOverlap = qBound(0.01, static_cast<double>(gridState.verticalOverlap) / 100.0, 0.99);
    // « Automatique » coché : 0, chaque step applique sa propre règle (5 % de la tuile, 10 % pour overlap-estimation)
    setup.railMargin = gridState.automaticUncertainty ? 0.0 : qMax(0.0, gridState.uncertaintyMargin);
    setup.pipelineChoices = gridState.pipelineChoices;   // méthodes de l'onglet Chaîne
    setup.lightGlueModelPath = FrontendUtils::lightGlueModelPath();   // absolu : le script ne dépend pas du dossier courant

    for (int index = 0; index < gridState.gridRows * gridState.gridColumns; ++index) {
        if (index >= gridState.gridTiles.size()) continue;
        const GridTile &tile = gridState.gridTiles.at(index);
        if (tile.empty || tile.path.isEmpty()) continue;
        setup.tiles.push_back({IONs::pathFromUtf8(tile.path.toStdString()),
                               index / gridState.gridColumns,
                               index % gridState.gridColumns});
    }
    return setup;
}

QString MainWindow::generatePipelineScript(int firstStep) const
{
    FrontendNs::ProjectSetupModelClass model;
    model.setSetup(buildCurrentProjectSetup());

    nlohmann::json script = model.createBackendScript();

    const auto steps = FrontendUtils::pipelineSteps(projectGrid->state().pipelineChoices);
    if (firstStep > 1 && firstStep <= static_cast<int>(steps.size())) {
        nlohmann::json completedStepsArray = nlohmann::json::array();
        for (int i = 0; i < firstStep - 1; ++i) {
            completedStepsArray.push_back(steps[static_cast<std::size_t>(i)].technicalName);
        }
        script["run"]["completedSteps"] = completedStepsArray;
        script["run"]["startFromStep"] = steps[static_cast<std::size_t>(firstStep - 1)].technicalName;
    }

    return QString::fromStdString(script.dump(2));
}

void MainWindow::requestRun(int firstStep)
{
    if (pipelineRunner->isRunning()) {
        updateStatus(tr("Une exécution du pipeline est déjà en cours."));
        return;
    }
    // Vérification préalable : au moins une tuile doit être chargée dans la grille.
    if (projectGrid->state().tilePaths.isEmpty()) {
        QMessageBox::warning(this, tr("Aucune tuile importée"),
            tr("Veuillez importer des tuiles avant de lancer le pipeline de calcul."));
        return;
    }
    // Demande confirmation si l'opérateur relance le calcul à partir d'une étape ultérieure.
    if (firstStep > 1 && !DialogHelpers::confirmRunRestart(this, firstStep)) {
        return;
    }

    QString scriptJson;
    try {
        scriptJson = generatePipelineScript(firstStep);
    } catch (const std::exception& exception) {
        QMessageBox::critical(this, tr("Erreur de configuration"),
            tr("Pipeline invalide : %1").arg(exception.what()));
        return;
    }
    if (scriptJson.isEmpty()) return;

    const auto steps = FrontendUtils::pipelineSteps(projectGrid->state().pipelineChoices);
    const QString startStep = (firstStep > 1 && firstStep <= static_cast<int>(steps.size()))
        ? QString::fromStdString(steps[static_cast<std::size_t>(firstStep - 1)].technicalName)
        : QString();

    // Transmet la configuration à la façade asynchrone puis démarre le calcul.
    pipelineRunner->setScriptJson(scriptJson);
    pipelineRunner->setStartFromStepName(startStep);
    pipelineRunner->startRun(firstStep);
}

void MainWindow::showComparisonDialog()
{
    DialogHelpers::showComparisonDialog(this, pipelineRunner);
}

void MainWindow::showCancelRunButton()
{
    if (workspaceUI->cancelRunButton())
        return;
    auto *cancelRunButton = FrontendUtils::makeButton(tr("Annuler"), this);
    workspaceUI->setCancelRunButton(cancelRunButton);
    cancelRunButton->setObjectName(QStringLiteral("cancelRunButton"));
    statusBar()->addPermanentWidget(cancelRunButton);
    connect(cancelRunButton, &QPushButton::clicked,
            pipelineRunner, &PipelineRunnerClass::cancelRun);
}

void MainWindow::removeCancelRunButton()
{
    QPushButton *cancelRunButton = workspaceUI->cancelRunButton();
    if (!cancelRunButton)
        return;
    statusBar()->removeWidget(cancelRunButton);
    cancelRunButton->deleteLater();
    workspaceUI->setCancelRunButton(nullptr);
}

void MainWindow::updateStatus(const QString &message)
{
    if (workspaceUI->statusLabel())
        workspaceUI->statusLabel()->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::updateProgress(const QString &stepName, int percentage)
{
    workspaceUI->currentStepLabel()->setText(stepName);
    workspaceUI->progressBar()->setValue(qBound(0, percentage, 100));
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab) {
        panelsVisible = !panelsVisible;
        workspaceUI->rightTabs()->setVisible(panelsVisible);
        workspaceUI->bottomTabs()->setVisible(panelsVisible);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// Sécurise la fermeture : si un calcul est en cours dans le QThread secondaire,
// demande confirmation à l'opérateur avant d'ordonner l'arrêt du worker pour éviter
// un arrêt brutal en pleine écriture de fichier ou une fuite de ressources.
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Si la fermeture a déjà été ordonnée et que le worker s'est arrêté, on accepte la fermeture
    if (isClosing) {
        event->accept();
        return;
    }

    if (pipelineRunner && pipelineRunner->isRunning()) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Calcul en cours"),
            tr("Un calcul du pipeline est en cours d'exécution.\n"
               "Voulez-vous vraiment l'interrompre et quitter l'application ?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            event->ignore();
            return;
        }

        // Empêche la fermeture immédiate de la fenêtre
        event->ignore();
        isClosing = true;
        setEnabled(false); // Bloque les entrées utilisateur sur la fenêtre

        // Affiche une boîte de dialogue indiquant que l'étape courante doit se terminer
        auto *progressDialog = new QProgressDialog(
            tr("Arrêt du pipeline après l'étape en cours...\nVeuillez patienter."),
            QString(), 0, 0, this);
        progressDialog->setWindowTitle(tr("Fermeture"));
        progressDialog->setWindowModality(Qt::WindowModal);
        progressDialog->setCancelButton(nullptr);
        progressDialog->show();

        // Ferme définitivement la fenêtre dès que le thread backend s'est arrêté
        auto finalizeClose = [this, progressDialog]() {
            progressDialog->close();
            this->close();
        };

        connect(pipelineRunner, &PipelineRunnerClass::runCancelled, this, finalizeClose);
        connect(pipelineRunner, &PipelineRunnerClass::runFinished, this, finalizeClose);

        pipelineRunner->cancelRun();
        return;
    }

    event->accept();
}
