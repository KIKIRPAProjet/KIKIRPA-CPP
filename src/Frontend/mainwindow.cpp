#include "mainwindow.h"
#include "artworkcanvas.h"
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
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QListWidget>
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
#include <QSignalBlocker>
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
        "QCheckBox { spacing:7px; }"));

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
            workspaceUI->currentStepLabel(), &QLabel::setText);
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
    updateStatus(tr("Mode : %1").arg(FrontendUtils::modeNames.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    const ProjectGrid::State &gridState = projectGrid->state();
    if (workspaceUI->projectNameLabel())
        workspaceUI->projectNameLabel()->setText(projectName);
    if (workspaceUI->projectNameEdit())
        workspaceUI->projectNameEdit()->setText(projectName);
    if (workspaceUI->projectFolderLabel()) {
        workspaceUI->projectFolderLabel()->setText(gridState.tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(gridState.tileDirectory));
    }
    if (workspaceUI->projectFormatLabel()) {
        const QFileInfoList folderFiles = gridState.tileDirectory.isEmpty()
            ? QFileInfoList()
            : QDir(gridState.tileDirectory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                QDir::Name | QDir::IgnoreCase);
        int validFiles = 0;
        for (const QFileInfo &file : folderFiles) {
            const QString suffix = file.suffix().toLower();
            if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
                ++validFiles;
        }
        const bool allFilesValid = !folderFiles.isEmpty() && validFiles == folderFiles.size();
        workspaceUI->projectFormatLabel()->setText(allFilesValid
            ? tr("Format : tous les éléments sont au format TIFF compatible (%1 fichier(s)).")
                  .arg(validFiles)
            : tr("Format : %1 fichier(s) TIFF compatible(s) sur %2 ; certains éléments "
                "ne sont pas au bon format.")
                  .arg(validFiles).arg(folderFiles.size()));
        workspaceUI->projectFormatLabel()->setStyleSheet(allFilesValid
            ? QStringLiteral("color:#66c98a;")
            : QStringLiteral("color:#e0a45f;"));
    }
    if (workspaceUI->tileCountLabel())
        workspaceUI->tileCountLabel()->setText(tr("%1 tuile(s) chargée(s)").arg(gridState.tilePaths.size()));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(workspaceUI->canvas()))
        canvasView->setImagePaths(gridState.tilePaths);
    refreshGrid();
    setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}

void MainWindow::refreshGrid()
{
    const ProjectGrid::State &gridState = projectGrid->state();
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(workspaceUI->canvas())) {
        canvasView->setGridState(gridState.gridRows, gridState.gridColumns,
                                 gridState.captureOrder, gridState.gridTiles,
                                 gridState.selectedGridCell);
    }
    refreshGridControls();
}

void MainWindow::refreshGridControls()
{
    const ProjectGrid::State &gridState = projectGrid->state();
    if (workspaceUI->gridRowsSpin()) {
        const QSignalBlocker blocker(workspaceUI->gridRowsSpin());
        workspaceUI->gridRowsSpin()->setValue(gridState.gridRows);
    }
    if (workspaceUI->gridColumnsSpin()) {
        const QSignalBlocker blocker(workspaceUI->gridColumnsSpin());
        workspaceUI->gridColumnsSpin()->setValue(gridState.gridColumns);
    }
    if (workspaceUI->captureOrderCombo()) {
        const QSignalBlocker blocker(workspaceUI->captureOrderCombo());
        workspaceUI->captureOrderCombo()->setCurrentIndex(gridState.captureOrder);
    }
    if (workspaceUI->arrangementCombo()) {
        const QSignalBlocker blocker(workspaceUI->arrangementCombo());
        workspaceUI->arrangementCombo()->setCurrentIndex(gridState.captureOrder);
    }
    if (workspaceUI->horizontalOverlapSpin()) {
        const QSignalBlocker blocker(workspaceUI->horizontalOverlapSpin());
        workspaceUI->horizontalOverlapSpin()->setValue(gridState.horizontalOverlap);
    }
    if (workspaceUI->verticalOverlapSpin()) {
        const QSignalBlocker blocker(workspaceUI->verticalOverlapSpin());
        workspaceUI->verticalOverlapSpin()->setValue(gridState.verticalOverlap);
    }
    if (workspaceUI->linkedOverlapCheck()) {
        const QSignalBlocker blocker(workspaceUI->linkedOverlapCheck());
        workspaceUI->linkedOverlapCheck()->setChecked(gridState.overlapsLinked);
    }
    if (workspaceUI->modalityCombo()) {
        const QSignalBlocker blocker(workspaceUI->modalityCombo());
        const int index = workspaceUI->modalityCombo()->findText(gridState.modality);
        workspaceUI->modalityCombo()->setCurrentIndex(index >= 0 ? index : workspaceUI->modalityCombo()->findText(tr("Autre")));
        if (index < 0)
            projectGrid->setModality(workspaceUI->modalityCombo()->currentText());
    }
    if (workspaceUI->scaleSpin()) {
        const QSignalBlocker blocker(workspaceUI->scaleSpin());
        workspaceUI->scaleSpin()->setValue(gridState.scaleMmPerPixel);
    }
    if (workspaceUI->automaticUncertaintyCheck()) {
        const QSignalBlocker blocker(workspaceUI->automaticUncertaintyCheck());
        workspaceUI->automaticUncertaintyCheck()->setChecked(gridState.automaticUncertainty);
    }
    if (workspaceUI->uncertaintySpin()) {
        const QSignalBlocker blocker(workspaceUI->uncertaintySpin());
        workspaceUI->uncertaintySpin()->setValue(gridState.uncertaintyMargin);
        workspaceUI->uncertaintySpin()->setEnabled(!gridState.automaticUncertainty);
    }
    const QString scaleText = gridState.scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(gridState.scaleMmPerPixel, 0, 'g', 8) : tr("à renseigner");
    if (auto *metadata = workspaceUI->workspace() ? workspaceUI->workspace()->findChild<QLabel *>(
                QStringLiteral("projectMetadataLabel")) : nullptr) {
        metadata->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                              .arg(gridState.modality, scaleText)
                              .arg(gridState.horizontalOverlap).arg(gridState.verticalOverlap));
    }
    if (auto *title = workspaceUI->workspace() ? workspaceUI->workspace()->findChild<QLabel *>(
                QStringLiteral("workspaceTitle")) : nullptr) {
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage")
                           .arg(projectName, gridState.modality));
    }

    int activeTiles = 0;
    for (int index = 0;
         index < qMin(gridState.gridRows * gridState.gridColumns,
                      static_cast<int>(gridState.gridTiles.size())); ++index) {
        const GridTile &tile = gridState.gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTiles;
    }
    if (workspaceUI->gridSummaryLabel()) {
        const QString validationStatus =
            gridState.gridValidated ? tr(" · grille validée") : QString();
        workspaceUI->gridSummaryLabel()->setText(tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                                      .arg(activeTiles).arg(gridState.gridRows).arg(gridState.gridColumns)
                                      .arg(validationStatus));
    }
    if (workspaceUI->selectedGridTileLabel()) {
        const int row = gridState.selectedGridCell / qMax(1, gridState.gridColumns);
        const int column = gridState.selectedGridCell % qMax(1, gridState.gridColumns);
        const GridTile tile = gridState.selectedGridCell >= 0 &&
                gridState.selectedGridCell < gridState.gridTiles.size()
            ? gridState.gridTiles.at(gridState.selectedGridCell) : GridTile{};
        const QString label = QStringLiteral("r%1_c%2").arg(row, 2, 10, QLatin1Char('0'))
                                 .arg(column, 2, 10, QLatin1Char('0'));
        if (tile.path.isEmpty()) {
            workspaceUI->selectedGridTileLabel()->setText(tile.empty
                ? tr("Case sélectionnée : %1 (vide)").arg(label)
                : tr("Case sélectionnée : %1").arg(label));
        } else {
            workspaceUI->selectedGridTileLabel()->setText(
                tr("Sélection : %1 · %2 · %3° · %4")
                    .arg(label, QFileInfo(tile.path).fileName())
                    .arg(tile.rotation)
                    .arg(tile.empty ? tr("vide") : (tile.flipped ? tr("retournée") : tr("normale"))));
        }
    }
    if (workspaceUI->emptyCellButton()) {
        const bool canRestore = gridState.selectedGridCell >= 0 &&
                gridState.selectedGridCell < gridState.gridTiles.size()
            && gridState.gridTiles.at(gridState.selectedGridCell).empty;
        workspaceUI->emptyCellButton()->setText(canRestore ? tr("Restaurer la tuile")
                                            : tr("Marquer une case vide"));
    }
    if (workspaceUI->referencePathLabel()) {
        workspaceUI->referencePathLabel()->setText(gridState.referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1")
                  .arg(QFileInfo(gridState.referenceImagePath).fileName()));
        workspaceUI->referencePathLabel()->setToolTip(gridState.referenceImagePath);
    }
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comparer avec l'image de référence"));
    dialog.resize(980, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *imagesLayout = new QHBoxLayout;
    const auto addImageColumn = [&imagesLayout](const QString &title, const QString &path,
                                                const QImage &image) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        auto *name = new QLabel(title + QStringLiteral("\n") + QFileInfo(path).fileName(), column);
        name->setWordWrap(true);
        auto *preview = new QLabel(column);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(400, 440);
        preview->setStyleSheet(QStringLiteral("background:#202020;"));
        preview->setPixmap(QPixmap::fromImage(image));
        columnLayout->addWidget(name);
        columnLayout->addWidget(preview, 1);
        imagesLayout->addWidget(column, 1);
    };
    addImageColumn(tr("Image de référence"), referencePath, referenceImage);
    addImageColumn(tr("Tuile sélectionnée"), tilePath, tileImage);
    layout->addLayout(imagesLayout, 1);
    layout->addWidget(FrontendUtils::mutedLabel(
        tr("Comparaison visuelle côte à côte ; aucune similarité automatique n'est calculée."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
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
        QDialog confirmation(this);
        confirmation.setWindowTitle(tr("Relancer à partir de l'étape %1").arg(firstStep));
        auto *layout = new QVBoxLayout(&confirmation);
        layout->addWidget(new QLabel(tr("Les étapes précédentes seront réutilisées sans recalcul."), &confirmation));
        auto *reused = new QListWidget(&confirmation);
        for (int step = 1; step < firstStep; ++step)
            reused->addItem(tr("✓  Étape %1 — résultat conservé").arg(step));
        layout->addWidget(reused);
        auto *recalculated = new QListWidget(&confirmation);
        for (int step = firstStep; step <= 8; ++step)
            recalculated->addItem(tr("↻  Étape %1 — résultat recalculé").arg(step));
        layout->addWidget(recalculated);
        layout->addWidget(FrontendUtils::mutedLabel(
            tr("Les résultats actuels seront conservés dans l'historique. "
               "Cette maquette ne lance pas de calcul backend."), &confirmation));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                             &confirmation);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Lancer"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
        if (confirmation.exec() != QDialog::Accepted)
            return;
    }
    simulationRunner->startRun(firstStep);
}

void MainWindow::showComparisonDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comparer deux exécutions"));
    dialog.resize(440, 240);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Comparer les sorties de deux algorithmes de couture."), &dialog));
    auto *algorithms = new QHBoxLayout;
    auto *a = new QComboBox(&dialog);
    auto *b = new QComboBox(&dialog);
    const QStringList choices = {tr("Programmation dynamique"), tr("Coupe de graphe"), tr("Dijkstra")};
    a->addItems(choices);
    b->addItems(choices);
    b->setCurrentIndex(1);
    algorithms->addWidget(a);
    algorithms->addWidget(b);
    layout->addLayout(algorithms);
    layout->addWidget(new QLabel(tr("Mesures : saut à la couture · détails conservés · durée"), &dialog));
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *show = buttons->addButton(tr("Comparer"), QDialogButtonBox::ActionRole);
    auto *keepA = buttons->addButton(tr("Garder A"), QDialogButtonBox::ActionRole);
    auto *keepB = buttons->addButton(tr("Garder B"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(show, &QPushButton::clicked, this, [this, &dialog]() {
        simulationRunner->toggleComparison();
        dialog.accept();
    });
    connect(keepA, &QPushButton::clicked, this, [this, &dialog]() {
        simulationRunner->keepComparisonA();
        dialog.accept();
    });
    connect(keepB, &QPushButton::clicked, this, [this, &dialog]() {
        simulationRunner->keepComparisonB();
        dialog.accept();
    });
    dialog.exec();
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
