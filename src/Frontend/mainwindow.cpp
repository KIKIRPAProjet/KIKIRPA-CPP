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
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "dialog_helpers.h"
#include "frontendutils.h"
#include "homepage.h"
#include "menubar_manager.h"
#include "projectgrid.h"
#include "project_serializer.h"
#include "simulationrunner.h"
#include "workspace_ui.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QFrame>
#include <QPixmap>
#include <QtMath>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <wincodec.h>
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("KIK-IRPA Assemblage"));
    resize(1228, 780);
    setMinimumSize(920, 600);
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
        "QPushButton:checked { background:#292929; border-color:#353535; color:#999; }"
        "QTabWidget::pane { border:1px solid #424242; }"
        "QTabBar::tab { background:#303030; color:#aaa; padding:7px 12px; }"
        "QTabBar::tab:selected { background:#3b3b3b; color:white; border-bottom:2px solid #438bca; }"
        "QTableWidget,QListWidget { background:#303030; alternate-background-color:#363636;"
        " gridline-color:#484848; selection-background-color:#36536d; }"
        "QHeaderView::section { background:#303030; color:#bdbdbd; border:0;"
        " border-bottom:1px solid #505050; padding:5px; }"
        "QProgressBar { background:#242424; border:0; text-align:center; height:8px; }"
        "QProgressBar::chunk { background:#43a56c; }"
        "QSplitter::handle { background:#202020; }"
        "QSplitter::handle:hover { background:#438bca; }"
        "QLabel[muted=\"true\"] { color:#999; }"
        "QCheckBox { spacing:7px; }"
    ));

    projectGrid = new ProjectGrid(this);
    simulationRunner = new SimulationRunner(this);
    workspaceUI = new WorkspaceUI(this, new HomePage, projectGrid, simulationRunner);
    menuBarManager = new MenuBarManager(this);
    menuBarManager->setupMenus(menuBar());
    refreshGrid();
    refreshProjectInfo();
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
    connect(simulationRunner, &SimulationRunner::runStarted, this, [this]() {
        workspaceUI->progressBar()->setValue(0);
    });
    connect(simulationRunner, &SimulationRunner::progressAdvanced,
            workspaceUI->progressBar(), &QProgressBar::setValue);
    connect(simulationRunner, &SimulationRunner::stepChanged,
            this, [this](int stepNumber, const QString &stepName) {
                workspaceUI->currentStepLabel()->setText(stepName);
                workspaceUI->setActiveStep(stepNumber);
            });
    connect(simulationRunner, &SimulationRunner::statusChanged,
            this, &MainWindow::updateStatus);
    connect(simulationRunner, &SimulationRunner::cancellationAvailable,
            this, &MainWindow::showCancelRunButton);
    connect(simulationRunner, &SimulationRunner::runFinished,
            this, &MainWindow::removeCancelRunButton);
    connect(simulationRunner, &SimulationRunner::runCancelled,
            this, &MainWindow::removeCancelRunButton);
    connect(simulationRunner, &SimulationRunner::comparisonRequested,
            this, &MainWindow::showComparisonDialog);
    connect(simulationRunner, &SimulationRunner::comparisonVisibilityChanged,
            this, [this](bool visible) {
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setComparisonVisible(visible);
    });
    connect(simulationRunner, &SimulationRunner::logMessage,
        workspaceUI, &WorkspaceUI::appendLog);
    connect(simulationRunner, &SimulationRunner::mosaicReady,
        this, [this](const QImage &image) {
            finalMosaic = image;
            if (auto *canvas = dynamic_cast<ArtworkCanvas*>(workspaceUI->canvas())) {
                canvas->setMosaicPreview(image);
                setMode(5); // 5 = Mode "Export", forcera l'affichage de l'image
                updateStatus(tr("Mosaïque finale générée (simulation)."));
            }
        }
    );
    connect(workspaceUI->homePage(), &HomePage::projectCreationRequested,
            this, &MainWindow::initializeNewProject);
    connect(workspaceUI->homePage(), &HomePage::projectLoaded,
            this, &MainWindow::applyLoadedProject);
    connect(workspaceUI->homePage(), &HomePage::currentProjectRequested,
            this, &MainWindow::openCurrentProjectFromHome);

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
        workspaceUI->zoomLabel()->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setZoom(zoomPercent);
    });
    connect(menuBarManager, &MenuBarManager::zoomOutRequested, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 10);
        workspaceUI->zoomLabel()->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setZoom(zoomPercent);
    });
    connect(menuBarManager, &MenuBarManager::fitToWindowRequested, this, [this]() {
        zoomPercent = 100;
        workspaceUI->zoomLabel()->setText(QStringLiteral("Ajuster"));
        static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setZoom(zoomPercent);
    });
    connect(menuBarManager, &MenuBarManager::modeChangeRequested, this, [this](int mode) {
        setMode(mode);
        if (mode == 1) {
            updateStatus(tr("Grille réorganisée selon l'ordre sélectionné."));
        } else if (mode == 2) {
            static_cast<ArtworkCanvas *>(workspaceUI->canvas())->setMaskVisible(true);
            updateStatus(tr("Proposition de masque affichée — à valider par l'opérateur."));
        }
    });
    connect(menuBarManager, &MenuBarManager::runRequested,
            this, &MainWindow::requestRun);
    connect(menuBarManager, &MenuBarManager::comparisonRequested,
            simulationRunner, &SimulationRunner::showComparison);
    connect(menuBarManager, &MenuBarManager::aboutRequested, this, [this]() {
        QMessageBox::about(this, tr("À propos"),
            tr("KIK-IRPA Assemblage — prototype d'interface.\n"
               "Les traitements d'image nécessitent le backend, qui n'est pas encore connecté."));
    });

    connect(workspaceUI->pages(), &QStackedWidget::currentChanged, this, [this](int index) {
        menuBar()->setVisible(index != 0);
    });
    menuBar()->setVisible(false);
}

void MainWindow::initializeNewProject(const QString &name, const QString &selectedTileDirectory,
                                      const QStringList &newTilePaths)
{
    finalMosaic = QImage();
    projectName = name;
    outputDirectory.clear();
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
    ProjectData projectData;
    QString errorMessage;
    if (!ProjectSerializer::loadFromFile(path, &projectData, &errorMessage)) {
        QMessageBox::warning(this, tr("Ouverture impossible"), errorMessage);
        return;
    }

    finalMosaic = QImage();
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
    setMode(0);
    workspaceUI->homePage()->setCurrentProjectInfo(
        projectCreated, projectPath, projectName, projectGrid->state().tilePaths.size());
    workspaceUI->homePage()->rememberRecentProject(path);
    updateStatus(tr("Projet ouvert sans recalcul. Vérifiez les chemins des tuiles."));
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
    if (workspaceUI->rightTabs() && workspaceUI->gridTabPage()) {
        const int gridIndex = workspaceUI->rightTabs()->indexOf(workspaceUI->gridTabPage());
        if (currentMode == 1 && gridIndex < 0) {
            workspaceUI->rightTabs()->insertTab(0, workspaceUI->gridTabPage(), tr("Grille"));
            workspaceUI->rightTabs()->setCurrentWidget(workspaceUI->gridTabPage());
        } else if (currentMode != 1 && gridIndex >= 0) {
            const bool gridWasSelected = workspaceUI->rightTabs()->currentWidget() == workspaceUI->gridTabPage();
            workspaceUI->rightTabs()->removeTab(gridIndex);
            if (gridWasSelected && workspaceUI->rightTabs()->count() > 0)
                workspaceUI->rightTabs()->setCurrentIndex(qMin(gridIndex, workspaceUI->rightTabs()->count() - 1));
        }
    }
    if (auto *button = workspaceUI->workspace()->findChild<QPushButton *>(QStringLiteral("mode%1").arg(currentMode)))
        button->setChecked(true);
    const QStringList descriptions = {
        tr("Projet — aperçu général"), tr("Vue Grille de tuiles — organiser et vérifier l'import"),
        tr("Tuile r00_c00 — rouge : bords sombres proposés · bleu : lasso en cours"),
        tr("Vue Paire — points de contrôle et correction"), tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"),
        tr("Export — TIFF 16 bits, profil ICC conservé")
    };
    if (auto *label = workspaceUI->workspace()->findChild<QLabel *>(QStringLiteral("modeDescription")))
        label->setText(descriptions.at(currentMode));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(workspaceUI->canvas()))
        canvasView->setMode(currentMode);
    if (workspaceUI->projectInfoPanel())
        workspaceUI->projectInfoPanel()->setVisible(currentMode == 0);
    if (workspaceUI->canvas())
        workspaceUI->canvas()->setVisible(currentMode != 0);
    if (auto *canvasControls = workspaceUI->workspace()->findChild<QWidget *>(
                QStringLiteral("canvasControls"))) {
        canvasControls->setVisible(currentMode != 0);
    }
    workspaceUI->arrangementCombo()->setVisible(currentMode == 1);
    if (currentMode == 1)
        workspaceUI->bottomTabs()->setCurrentIndex(1);
    if (currentMode == 3)
        workspaceUI->bottomTabs()->setCurrentIndex(0);
    updateStatus(tr("Mode : %1").arg(FrontendUtils::MODE_NAMES.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    workspaceUI->updateFromGridState(projectName, projectGrid->state());
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

    updateStatus(tr("Grille validée : %1 tuile(s), %2 × %3, ordre « %4 ».")
                     .arg(activeTileCount).arg(gridState.gridRows).arg(gridState.gridColumns)
                     .arg(workspaceUI->captureOrderCombo() ? workspaceUI->captureOrderCombo()->currentText() : QString()));
    QMessageBox::information(this, tr("Grille validée"),
        tr("La grille et ses paramètres sont valides. Enregistrez le projet pour conserver les réglages."));
}

void MainWindow::handleTilesImported(const QString &directory, int importedCount)
{
    setMode(1);
    updateStatus(tr("%1 photo(s) importée(s) depuis « %2 ». Les images source ne sont pas modifiées.")
                     .arg(importedCount).arg(QFileInfo(directory).fileName()));
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
    if (!ProjectSerializer::saveToFile(
            path, projectName, outputDirectory, currentMode, gridState, &errorMessage)) {
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

void MainWindow::requestRun(int firstStep)
{
    if (simulationRunner->isRunning()) {
        updateStatus(tr("Une démonstration de progression est déjà en cours."));
        return;
    }
    if (projectGrid->state().tilePaths.isEmpty()) {
        const auto answer = QMessageBox::question(this, tr("Aucune tuile importée"),
            tr("Aucune tuile n'est chargée. Lancer quand même la démonstration d'interface ?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }
    if (firstStep > 1) {
        if (!DialogHelpers::confirmRunRestart(this, firstStep))
            return;
    }
    simulationRunner->startRun(firstStep);
}

void MainWindow::showComparisonDialog()
{
    DialogHelpers::showComparisonDialog(this, simulationRunner);
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
            simulationRunner, &SimulationRunner::cancelRun);
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
