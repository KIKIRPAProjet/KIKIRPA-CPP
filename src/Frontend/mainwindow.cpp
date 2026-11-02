#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QFrame>
#include <QTransform>
#include <QPixmap>
#include <QtMath>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <wincodec.h>
#include <windows.h>
#endif

namespace {
const QStringList modeNames = {
    QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
    QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Export")
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("primary", primary);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}

}

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

    pages = new QStackedWidget(this);
    setCentralWidget(pages);
    createHomePage();
    createWorkspace();

    auto *fileMenu = menuBar()->addMenu(tr("Fichier"));
    auto *newAction = fileMenu->addAction(tr("Nouveau projet…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::createProject);
    auto *openAction = fileMenu->addAction(tr("Ouvrir un projet…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    auto *importAction = fileMenu->addAction(tr("Importer un dossier de photos…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MainWindow::importTiles);
    auto *saveAction = fileMenu->addAction(tr("Enregistrer le projet"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    fileMenu->addSeparator();
    auto *exportAction = fileMenu->addAction(tr("Exporter / rapport…"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::showExportDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Retour à l’accueil"), this, [this]() {
        pages->setCurrentIndex(0);
    });
    fileMenu->addAction(tr("Quitter"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu(tr("Édition"));
    editMenu->addAction(tr("Annuler"), QKeySequence::Undo, this,
                        [this]() { updateStatus(tr("Dernière action annulée.")); });
    editMenu->addAction(tr("Rétablir"), QKeySequence::Redo, this,
                        [this]() { updateStatus(tr("Action rétablie.")); });
    auto *viewMenu = menuBar()->addMenu(tr("Affichage"));
    auto *togglePanels = viewMenu->addAction(tr("Afficher / masquer les panneaux"));
    connect(togglePanels, &QAction::triggered, this, [this]() {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
    });
    auto *zoomIn = viewMenu->addAction(tr("Zoom avant"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *zoomOut = viewMenu->addAction(tr("Zoom arrière"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *fit = viewMenu->addAction(tr("Ajuster à la fenêtre"));
    fit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fit, &QAction::triggered, this, [this]() {
        zoomPercent = 100;
        zoomLabel->setText(QStringLiteral("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *toolsMenu = menuBar()->addMenu(tr("Tuiles"));
    toolsMenu->addAction(tr("Importer un dossier de photos…"), this, &MainWindow::importTiles);
    toolsMenu->addAction(tr("Organiser la grille"), this, [this]() {
        setMode(1);
        updateStatus(tr("Grille réorganisée selon l'ordre sélectionné."));
    });
    auto *masksMenu = menuBar()->addMenu(tr("Masques"));
    masksMenu->addAction(tr("Détecter les bords noirs"), this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Proposition de masque affichée — à valider par l'opérateur."));
    });
    auto *assemblyMenu = menuBar()->addMenu(tr("Assemblage"));
    assemblyMenu->addAction(tr("Tout lancer"), QKeySequence(Qt::Key_F5), this,
                            [this]() { startRun(1); });
    assemblyMenu->addAction(tr("Relancer à partir de…"), this, [this]() { startRun(6); });
    assemblyMenu->addAction(tr("Comparer deux exécutions…"), this, &MainWindow::showComparison);
    auto *helpMenu = menuBar()->addMenu(tr("Aide"));
    helpMenu->addAction(tr("À propos"), this, [this]() {
        QMessageBox::about(this, tr("À propos"),
            tr("KIK-IRPA Assemblage — prototype d'interface.\n"
               "Les traitements d'image nécessitent le backend, qui n'est pas encore connecté."));
    });

    connect(pages, &QStackedWidget::currentChanged, this, [this](int index) {
        menuBar()->setVisible(index != 0);
    });
    menuBar()->setVisible(false);
}

void MainWindow::createHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *side = new QWidget;
    side->setFixedWidth(245);
    side->setStyleSheet(QStringLiteral("background:#202020;"));
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    auto *brand = new QLabel(tr("KIK-IRPA Assemblage"));
    brand->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;color:#d5d5d5;"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(14);
    sideLayout->addWidget(mutedLabel(tr("Assemblage de tuiles haute résolution\n"
                                        "de peintures planes."), side));
    sideLayout->addSpacing(16);
    auto *newProject = makeButton(tr("+ Nouveau projet…"), side, true);
    sideLayout->addWidget(newProject);
    auto *open = makeButton(tr("Importer un projet…"), side);
    sideLayout->addWidget(open);
    sideLayout->addStretch();
    sideLayout->addWidget(mutedLabel(tr("Manuel d'utilisation · Raccourcis clavier · À propos"), side));
    layout->addWidget(side);

    auto *recentArea = new QWidget;
    auto *recentLayout = new QVBoxLayout(recentArea);
    recentLayout->setContentsMargins(30, 26, 30, 24);
    recentLayout->addWidget(new QLabel(tr("Projets récents")));
    recentEmptyLabel = mutedLabel(tr("Aucun projet récent."), recentArea);
    recentLayout->addWidget(recentEmptyLabel);
    auto *cardsContainer = new QWidget(recentArea);
    recentCardsLayout = new QHBoxLayout(cardsContainer);
    recentCardsLayout->setContentsMargins(0, 0, 0, 0);
    recentCardsLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    recentLayout->addWidget(cardsContainer);
    recentLayout->addStretch();
    layout->addWidget(recentArea, 1);
    pages->addWidget(page);
    refreshRecentProjects();
    connect(newProject, &QPushButton::clicked, this, &MainWindow::createProject);
    connect(open, &QPushButton::clicked, this, &MainWindow::openProject);
}

void MainWindow::refreshRecentProjects()
{
    if (!recentCardsLayout || !recentEmptyLabel)
        return;

    while (QLayoutItem *item = recentCardsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    QStringList validPaths;
    for (const QString &path : recentPaths) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(absolutePath) && !validPaths.contains(absolutePath))
            validPaths.append(absolutePath);
    }
    if (validPaths != recentPaths) {
        recentPaths = validPaths;
        settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    }

    const auto formatModifiedDate = [](const QDateTime &dateTime) {
        return dateTime.isValid()
            ? dateTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))
            : QObject::tr("Date inconnue");
    };

    int cardCount = 0;
    if (projectCreated && projectPath.isEmpty()) {
        auto *card = makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(projectName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le : %2")
                                       .arg(tilePaths.size())
                                       .arg(tr("Non enregistré")), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this]() {
            pages->setCurrentWidget(workspace);
            setMode(1);
            updateStatus(tr("Projet courant : %1").arg(projectName));
        });
        ++cardCount;
    }

    for (const QString &path : recentPaths) {
        QFile file(path);
        QString recentName = QFileInfo(path).completeBaseName();
        int recentTileCount = 0;
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) {
                const QJsonObject project = document.object();
                recentName = project.value(QStringLiteral("project_name"))
                                 .toString(recentName);
                const QJsonArray paths = project.value(QStringLiteral("tiles")).toArray();
                recentTileCount = paths.size();
            }
        }

        auto *card = makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(recentName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        name->setWordWrap(true);
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le %2")
                                       .arg(recentTileCount)
                                       .arg(formatModifiedDate(QFileInfo(path).lastModified())), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this, path]() {
            loadProjectFromPath(path);
        });
        ++cardCount;
    }

    recentCardsLayout->addStretch();
    recentEmptyLabel->setVisible(cardCount == 0);
}

void MainWindow::rememberRecentProject(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo::exists(absolutePath))
        return;

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    recentPaths.removeAll(absolutePath);
    recentPaths.prepend(absolutePath);
    while (recentPaths.size() > 10)
        recentPaths.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    refreshRecentProjects();
}

void MainWindow::createProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nouveau projet"),
        tr("Nom du projet :"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    const QString selectedTileDirectory = QFileDialog::getExistingDirectory(
        this, tr("Choisir le dossier des tuiles"));
    if (selectedTileDirectory.isEmpty())
        return;

    const QFileInfoList tileFiles = QDir(selectedTileDirectory).entryInfoList(
        {QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
        QDir::Files, QDir::Name);

    projectName = name;
    tileDirectory = selectedTileDirectory;
    outputDirectory.clear();
    projectPath.clear();
    projectCreated = true;
    tilePaths.clear();
    for (const QFileInfo &tileFile : tileFiles)
        tilePaths.append(tileFile.absoluteFilePath());

    gridRows = 3;
    gridColumns = 2;
    gridRows = qMax(1, qMin(20, (static_cast<int>(tilePaths.size()) + gridColumns - 1)
                                     / gridColumns));
    captureOrder = 0;
    horizontalOverlap = 20;
    verticalOverlap = 20;
    overlapsLinked = true;
    modality = QStringLiteral("Infrarouge");
    scaleMmPerPixel = 0.0;
    uncertaintyMargin = 0.0;
    automaticUncertainty = true;
    referenceImagePath.clear();
    selectedGridCell = 0;
    gridValidated = false;
    initializeGridFromPaths();

    pages->setCurrentWidget(workspace);
    setMode(0);
    refreshProjectInfo();
    if (tilePaths.isEmpty()) {
        updateStatus(tr("Projet créé. Aucun fichier image reconnu dans le dossier des tuiles."));
    } else {
        updateStatus(tr("Projet créé avec %1 tuile(s) trouvée(s).").arg(tilePaths.size()));
    }
    refreshRecentProjects();
}

void MainWindow::setMode(int mode)
{
    currentMode = qBound(0, mode, 5);
    pages->setCurrentWidget(workspace);
    if (rightTabs && gridTabPage) {
        const int gridIndex = rightTabs->indexOf(gridTabPage);
        if (currentMode == 1 && gridIndex < 0) {
            rightTabs->insertTab(0, gridTabPage, tr("Grille"));
            rightTabs->setCurrentWidget(gridTabPage);
        } else if (currentMode != 1 && gridIndex >= 0) {
            const bool gridWasSelected = rightTabs->currentWidget() == gridTabPage;
            rightTabs->removeTab(gridIndex);
            if (gridWasSelected && rightTabs->count() > 0)
                rightTabs->setCurrentIndex(qMin(gridIndex, rightTabs->count() - 1));
        }
    }
    if (auto *button = workspace->findChild<QPushButton *>(QStringLiteral("mode%1").arg(currentMode)))
        button->setChecked(true);
    const QStringList descriptions = {
        tr("Projet — aperçu général"), tr("Vue Grille de tuiles — organiser et vérifier l'import"),
        tr("Tuile r00_c00 — rouge : bords sombres proposés · bleu : lasso en cours"),
        tr("Vue Paire — points de contrôle et correction"), tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"),
        tr("Export — TIFF 16 bits, profil ICC conservé")
    };
    if (auto *label = workspace->findChild<QLabel *>(QStringLiteral("modeDescription")))
        label->setText(descriptions.at(currentMode));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas))
        canvasView->setMode(currentMode);
    if (projectInfoPanel)
        projectInfoPanel->setVisible(currentMode == 0);
    if (canvas)
        canvas->setVisible(currentMode != 0);
    if (auto *canvasControls = workspace->findChild<QWidget *>(
                QStringLiteral("canvasControls"))) {
        canvasControls->setVisible(currentMode != 0);
    }
    arrangementCombo->setVisible(currentMode == 1);
    if (currentMode == 1)
        bottomTabs->setCurrentIndex(1);
    if (currentMode == 3)
        bottomTabs->setCurrentIndex(0);
    updateStatus(tr("Mode : %1").arg(modeNames.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    if (projectNameLabel)
        projectNameLabel->setText(projectName);
    if (projectNameEdit)
        projectNameEdit->setText(projectName);
    if (projectFolderLabel) {
        projectFolderLabel->setText(tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(tileDirectory));
    }
    if (projectFormatLabel) {
        const QFileInfoList folderFiles = tileDirectory.isEmpty()
            ? QFileInfoList()
            : QDir(tileDirectory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                QDir::Name | QDir::IgnoreCase);
        int validFiles = 0;
        for (const QFileInfo &file : folderFiles) {
            const QString suffix = file.suffix().toLower();
            if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
                ++validFiles;
        }
        const bool allFilesValid = !folderFiles.isEmpty() && validFiles == folderFiles.size();
        projectFormatLabel->setText(allFilesValid
            ? tr("Format : tous les éléments sont au format TIFF compatible (%1 fichier(s)).")
                  .arg(validFiles)
            : tr("Format : %1 fichier(s) TIFF compatible(s) sur %2 ; certains éléments "
                "ne sont pas au bon format.")
                  .arg(validFiles).arg(folderFiles.size()));
        projectFormatLabel->setStyleSheet(allFilesValid
            ? QStringLiteral("color:#66c98a;")
            : QStringLiteral("color:#e0a45f;"));
    }
    if (tileCountLabel)
        tileCountLabel->setText(tr("%1 tuile(s) chargée(s)").arg(tilePaths.size()));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas))
        canvasView->setImagePaths(tilePaths);
    refreshGrid();
    setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}

void MainWindow::initializeGridFromPaths()
{
    if (tilePaths.size() > gridRows * gridColumns)
        gridRows = qMin(20, (static_cast<int>(tilePaths.size()) + gridColumns - 1) / gridColumns);

    const int cellCount = qMax(gridRows * gridColumns, static_cast<int>(tilePaths.size()));
    gridTiles = QVector<GridTile>(cellCount);
    const QVector<int> positions = captureOrderCells(gridRows, gridColumns, captureOrder);
    for (int index = 0; index < tilePaths.size(); ++index) {
        const int cell = index < positions.size() ? positions.at(index) : index;
        gridTiles[cell].path = tilePaths.at(index);
    }
    if (gridRowsSpin)
        gridRowsSpin->setValue(gridRows);
    if (gridColumnsSpin)
        gridColumnsSpin->setValue(gridColumns);
    selectedGridCell = 0;
}

void MainWindow::refreshGrid()
{
    while (gridTiles.size() < gridRows * gridColumns)
        gridTiles.append(GridTile{});
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas)) {
        canvasView->setGridState(gridRows, gridColumns, captureOrder,
                                 gridTiles, selectedGridCell);
    }
    refreshGridControls();
}

void MainWindow::refreshGridControls()
{
    if (gridRowsSpin) {
        const QSignalBlocker blocker(gridRowsSpin);
        gridRowsSpin->setValue(gridRows);
    }
    if (gridColumnsSpin) {
        const QSignalBlocker blocker(gridColumnsSpin);
        gridColumnsSpin->setValue(gridColumns);
    }
    if (captureOrderCombo) {
        const QSignalBlocker blocker(captureOrderCombo);
        captureOrderCombo->setCurrentIndex(captureOrder);
    }
    if (arrangementCombo) {
        const QSignalBlocker blocker(arrangementCombo);
        arrangementCombo->setCurrentIndex(captureOrder);
    }
    if (horizontalOverlapSpin) {
        const QSignalBlocker blocker(horizontalOverlapSpin);
        horizontalOverlapSpin->setValue(horizontalOverlap);
    }
    if (verticalOverlapSpin) {
        const QSignalBlocker blocker(verticalOverlapSpin);
        verticalOverlapSpin->setValue(verticalOverlap);
    }
    if (linkedOverlapCheck) {
        const QSignalBlocker blocker(linkedOverlapCheck);
        linkedOverlapCheck->setChecked(overlapsLinked);
    }
    if (modalityCombo) {
        const QSignalBlocker blocker(modalityCombo);
        const int index = modalityCombo->findText(modality);
        modalityCombo->setCurrentIndex(index >= 0 ? index : modalityCombo->findText(tr("Autre")));
        if (index < 0)
            modality = modalityCombo->currentText();
    }
    if (scaleSpin) {
        const QSignalBlocker blocker(scaleSpin);
        scaleSpin->setValue(scaleMmPerPixel);
    }
    if (automaticUncertaintyCheck) {
        const QSignalBlocker blocker(automaticUncertaintyCheck);
        automaticUncertaintyCheck->setChecked(automaticUncertainty);
    }
    if (uncertaintySpin) {
        const QSignalBlocker blocker(uncertaintySpin);
        uncertaintySpin->setValue(uncertaintyMargin);
        uncertaintySpin->setEnabled(!automaticUncertainty);
    }
    const QString scaleText = scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(scaleMmPerPixel, 0, 'g', 8) : tr("à renseigner");
    if (auto *metadata = workspace ? workspace->findChild<QLabel *>(
                QStringLiteral("projectMetadataLabel")) : nullptr) {
        metadata->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                              .arg(modality, scaleText)
                              .arg(horizontalOverlap).arg(verticalOverlap));
    }
    if (auto *title = workspace ? workspace->findChild<QLabel *>(
                QStringLiteral("workspaceTitle")) : nullptr) {
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage").arg(projectName, modality));
    }

    int activeTiles = 0;
    for (int index = 0; index < qMin(gridRows * gridColumns, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTiles;
    }
    if (gridSummaryLabel) {
        const QString validationStatus = gridValidated ? tr(" · grille validée") : QString();
        gridSummaryLabel->setText(tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                                      .arg(activeTiles).arg(gridRows).arg(gridColumns)
                                      .arg(validationStatus));
    }
    if (selectedGridTileLabel) {
        const int row = selectedGridCell / qMax(1, gridColumns);
        const int column = selectedGridCell % qMax(1, gridColumns);
        const GridTile tile = selectedGridCell >= 0 && selectedGridCell < gridTiles.size()
            ? gridTiles.at(selectedGridCell) : GridTile{};
        const QString label = QStringLiteral("r%1_c%2").arg(row, 2, 10, QLatin1Char('0'))
                                 .arg(column, 2, 10, QLatin1Char('0'));
        if (tile.path.isEmpty()) {
            selectedGridTileLabel->setText(tile.empty
                ? tr("Case sélectionnée : %1 (vide)").arg(label)
                : tr("Case sélectionnée : %1").arg(label));
        } else {
            selectedGridTileLabel->setText(
                tr("Sélection : %1 · %2 · %3° · %4")
                    .arg(label, QFileInfo(tile.path).fileName())
                    .arg(tile.rotation)
                    .arg(tile.empty ? tr("vide") : (tile.flipped ? tr("retournée") : tr("normale"))));
        }
    }
    if (emptyCellButton) {
        const bool canRestore = selectedGridCell >= 0 && selectedGridCell < gridTiles.size()
            && gridTiles.at(selectedGridCell).empty;
        emptyCellButton->setText(canRestore ? tr("Restaurer la tuile")
                                            : tr("Marquer une case vide"));
    }
    if (referencePathLabel) {
        referencePathLabel->setText(referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1").arg(QFileInfo(referenceImagePath).fileName()));
        referencePathLabel->setToolTip(referenceImagePath);
    }
}

void MainWindow::swapGridTiles(int first, int second)
{
    const int cellCount = gridRows * gridColumns;
    if (first < 0 || second < 0 || first >= cellCount || second >= cellCount)
        return;
    while (gridTiles.size() <= qMax(first, second))
        gridTiles.append(GridTile{});
    std::swap(gridTiles[first], gridTiles[second]);
    selectedGridCell = second;
    gridValidated = false;
    refreshGrid();
}

void MainWindow::importReferenceImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer une image de référence"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;Tous les fichiers (*)"));
    if (path.isEmpty())
        return;
    if (readPreviewImage(path, QSize(600, 500)).isNull()) {
        QMessageBox::warning(this, tr("Image illisible"),
            tr("L'image de référence ne peut pas être ouverte."));
        return;
    }
    referenceImagePath = QFileInfo(path).absoluteFilePath();
    gridValidated = false;
    refreshGridControls();
    updateStatus(tr("Image de référence importée : %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::compareReferenceImage()
{
    if (referenceImagePath.isEmpty()) {
        importReferenceImage();
        if (referenceImagePath.isEmpty())
            return;
    }
    if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
        gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
        QMessageBox::information(this, tr("Aucune tuile sélectionnée"),
            tr("Sélectionnez une tuile non vide dans la grille avant de la comparer."));
        return;
    }

    const QString tilePath = gridTiles.at(selectedGridCell).path;
    const QImage reference = readPreviewImage(referenceImagePath, QSize(420, 400));
    QImage tile = readPreviewImage(tilePath, QSize(420, 400));
    if (reference.isNull() || tile.isNull()) {
        QMessageBox::warning(this, tr("Comparaison impossible"),
            tr("L'image de référence ou la tuile sélectionnée ne peut pas être lue."));
        return;
    }
    const GridTile selectedTile = gridTiles.at(selectedGridCell);
    if (selectedTile.flipped)
        tile = tile.mirrored(true, false);
    if (selectedTile.rotation != 0)
        tile = tile.transformed(QTransform().rotate(selectedTile.rotation),
                                Qt::SmoothTransformation);

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
    addImageColumn(tr("Image de référence"), referenceImagePath, reference);
    addImageColumn(tr("Tuile sélectionnée"), tilePath, tile);
    layout->addLayout(imagesLayout, 1);
    layout->addWidget(mutedLabel(
        tr("Comparaison visuelle côte à côte ; aucune similarité automatique n'est calculée."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    updateStatus(tr("Comparaison visuelle terminée."));
}

void MainWindow::validateGrid()
{
    QStringList errors;
    const int cellCount = gridRows * gridColumns;
    QSet<QString> assignedPaths;
    int activeTiles = 0;
    for (int index = cellCount; index < gridTiles.size(); ++index) {
        if (!gridTiles.at(index).path.isEmpty())
            errors.append(tr("La tuile %1 est hors de la grille.")
                              .arg(QFileInfo(gridTiles.at(index).path).fileName()));
    }
    for (int index = 0; index < qMin(cellCount, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (tile.path.isEmpty())
            continue;
        const QString absolutePath = QFileInfo(tile.path).absoluteFilePath();
        if (assignedPaths.contains(absolutePath)) {
            errors.append(tr("La même image est affectée à plusieurs cases : %1")
                              .arg(QFileInfo(tile.path).fileName()));
        }
        assignedPaths.insert(absolutePath);
        if (!QFileInfo::exists(tile.path))
            errors.append(tr("Fichier introuvable : %1").arg(tile.path));
        else if (readPreviewImage(tile.path, QSize(32, 32)).isNull())
            errors.append(tr("Image illisible : %1").arg(tile.path));
        if (!tile.empty)
            ++activeTiles;
    }
    for (const QString &path : tilePaths) {
        if (!assignedPaths.contains(QFileInfo(path).absoluteFilePath()))
            errors.append(tr("Image non placée dans la grille : %1")
                              .arg(QFileInfo(path).fileName()));
    }
    if (activeTiles == 0)
        errors.append(tr("La grille doit contenir au moins une tuile active."));

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("Grille à corriger"), errors.join(QLatin1Char('\n')));
        updateStatus(tr("La grille n'est pas valide : %1 problème(s).").arg(errors.size()));
        gridValidated = false;
        refreshGridControls();
        return;
    }

    gridValidated = true;
    refreshGridControls();
    updateStatus(tr("Grille validée : %1 tuile(s), %2 × %3, ordre « %4 ».")
                     .arg(activeTiles).arg(gridRows).arg(gridColumns)
                     .arg(captureOrderCombo ? captureOrderCombo->currentText() : QString()));
    QMessageBox::information(this, tr("Grille validée"),
        tr("La grille et ses paramètres sont valides. Enregistrez le projet pour conserver les réglages."));
}

void MainWindow::importTiles()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, tr("Sélectionner un dossier de photos"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directoryPath.isEmpty())
        return;

    const QDir directory(directoryPath);
    const QStringList imageFilters = {QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
    const QFileInfoList imageFiles = directory.entryInfoList(
        imageFilters, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    if (imageFiles.isEmpty()) {
        QMessageBox::warning(this, tr("Aucune photo trouvée"),
            tr("Le dossier sélectionné ne contient aucune image TIFF compatible "
               "(.tif ou .tiff)."));
        return;
    }

    QStringList paths;
    paths.reserve(imageFiles.size());
    for (const QFileInfo &imageFile : imageFiles)
        if (!tilePaths.contains(imageFile.absoluteFilePath()))
            paths.append(imageFile.absoluteFilePath());

    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("Photos déjà importées"),
            tr("Toutes les photos de ce dossier sont déjà dans le projet."));
        return;
    }

    tilePaths.append(paths);
    tileDirectory = directoryPath;
    const int requiredRows = (static_cast<int>(tilePaths.size()) + gridColumns - 1) / gridColumns;
    gridRows = qMax(gridRows, qMin(20, requiredRows));
    while (gridTiles.size() < gridRows * gridColumns)
        gridTiles.append(GridTile{});
    const QVector<int> positions = captureOrderCells(gridRows, gridColumns, captureOrder);
    int positionIndex = 0;
    for (const QString &path : paths) {
        while (positionIndex < positions.size() &&
               !gridTiles.at(positions.at(positionIndex)).path.isEmpty())
            ++positionIndex;
        if (positionIndex >= positions.size()) {
            GridTile overflowTile;
            overflowTile.path = path;
            gridTiles.append(overflowTile);
            continue;
        }
        gridTiles[positions.at(positionIndex)].path = path;
        ++positionIndex;
    }
    gridValidated = false;
    refreshProjectInfo();
    setMode(1);
    updateStatus(tr("%1 photo(s) importée(s) depuis « %2 ». Les images source ne sont pas modifiées.")
                 .arg(paths.size()).arg(directory.dirName()));
}

void MainWindow::saveProject()
{
    QString path = projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"),
            projectName + QStringLiteral(".kikproj"), tr("Projet KIK-IRPA (*.kikproj);;JSON (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject project;
    project.insert(QStringLiteral("format_version"), QStringLiteral("1.0"));
    project.insert(QStringLiteral("project_name"), projectName);
    project.insert(QStringLiteral("saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), currentMode);
    project.insert(QStringLiteral("tile_directory"), tileDirectory);
    project.insert(QStringLiteral("output_directory"), outputDirectory);
    project.insert(QStringLiteral("grid_rows"), gridRows);
    project.insert(QStringLiteral("grid_columns"), gridColumns);
    project.insert(QStringLiteral("capture_order"), captureOrder);
    project.insert(QStringLiteral("horizontal_overlap"), horizontalOverlap);
    project.insert(QStringLiteral("vertical_overlap"), verticalOverlap);
    project.insert(QStringLiteral("overlaps_linked"), overlapsLinked);
    project.insert(QStringLiteral("modality"), modality);
    project.insert(QStringLiteral("scale_mm_per_pixel"), scaleMmPerPixel);
    project.insert(QStringLiteral("uncertainty_margin"), uncertaintyMargin);
    project.insert(QStringLiteral("automatic_uncertainty"), automaticUncertainty);
    project.insert(QStringLiteral("reference_image"), referenceImagePath);
    QJsonArray tiles;
    for (const QString &tile : tilePaths)
        tiles.append(tile);
    project.insert(QStringLiteral("tiles"), tiles);
    QJsonArray grid;
    for (const GridTile &tile : gridTiles) {
        QJsonObject cell;
        cell.insert(QStringLiteral("path"), tile.path);
        cell.insert(QStringLiteral("rotation"), tile.rotation);
        cell.insert(QStringLiteral("flipped"), tile.flipped);
        cell.insert(QStringLiteral("empty"), tile.empty);
        grid.append(cell);
    }
    project.insert(QStringLiteral("grid_tiles"), grid);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
            tr("Le projet n'a pas pu être enregistré à cet emplacement."));
        return;
    }
    projectPath = path;
    projectCreated = true;
    rememberRecentProject(path);
    updateStatus(tr("Projet enregistré : %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"),
        QString(), tr("Projet KIK-IRPA (*.kikproj *.json);;Tous les fichiers (*)"));
    if (path.isEmpty())
        return;
    loadProjectFromPath(path);
}

bool MainWindow::loadProjectFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ouverture impossible"), tr("Le fichier ne peut pas être lu."));
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Projet invalide"),
            tr("Ce fichier n'est pas un projet JSON valide."));
        return false;
    }
    const QJsonObject project = document.object();
    projectName = project.value(QStringLiteral("project_name")).toString(QFileInfo(path).completeBaseName());
    tileDirectory = project.value(QStringLiteral("tile_directory")).toString();
    outputDirectory = project.value(QStringLiteral("output_directory")).toString();
    gridRows = qBound(1, project.value(QStringLiteral("grid_rows")).toInt(3), 20);
    gridColumns = qBound(1, project.value(QStringLiteral("grid_columns")).toInt(2), 20);
    captureOrder = qBound(0, project.value(QStringLiteral("capture_order")).toInt(0), 5);
    horizontalOverlap = qBound(0, project.value(QStringLiteral("horizontal_overlap")).toInt(20), 90);
    verticalOverlap = qBound(0, project.value(QStringLiteral("vertical_overlap")).toInt(20), 90);
    overlapsLinked = project.value(QStringLiteral("overlaps_linked")).toBool(true);
    if (overlapsLinked)
        verticalOverlap = horizontalOverlap;
    modality = project.value(QStringLiteral("modality")).toString(QStringLiteral("Infrarouge"));
    scaleMmPerPixel = qMax(0.0, project.value(QStringLiteral("scale_mm_per_pixel")).toDouble());
    uncertaintyMargin = qMax(0.0, project.value(QStringLiteral("uncertainty_margin")).toDouble());
    automaticUncertainty = project.value(QStringLiteral("automatic_uncertainty")).toBool(true);
    referenceImagePath = project.value(QStringLiteral("reference_image")).toString();
    tilePaths.clear();
    for (const QJsonValue &tile : project.value(QStringLiteral("tiles")).toArray())
        tilePaths.append(tile.toString());
    const QJsonArray grid = project.value(QStringLiteral("grid_tiles")).toArray();
    if (grid.isEmpty()) {
        initializeGridFromPaths();
    } else {
        gridTiles.clear();
        gridTiles.reserve(qMax(gridRows * gridColumns, static_cast<int>(grid.size())));
        for (const QJsonValue &cellValue : grid) {
            const QJsonObject cell = cellValue.toObject();
            GridTile tile;
            tile.path = cell.value(QStringLiteral("path")).toString();
            const int savedRotation = cell.value(QStringLiteral("rotation")).toInt();
            tile.rotation = ((savedRotation % 360) + 360) % 360;
            tile.flipped = cell.value(QStringLiteral("flipped")).toBool();
            tile.empty = cell.value(QStringLiteral("empty")).toBool();
            gridTiles.append(tile);
        }
        while (gridTiles.size() < gridRows * gridColumns)
            gridTiles.append(GridTile{});
    }
    selectedGridCell = 0;
    gridValidated = false;
    projectPath = path;
    projectCreated = true;
    refreshProjectInfo();
    pages->setCurrentWidget(workspace);
    setMode(0);
    rememberRecentProject(path);
    updateStatus(tr("Projet ouvert sans recalcul. Vérifiez les chemins des tuiles."));
    return true;
}

void MainWindow::startRun(int firstStep)
{
    if (progressRunning) {
        updateStatus(tr("Une démonstration de progression est déjà en cours."));
        return;
    }
    if (tilePaths.isEmpty()) {
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
        layout->addWidget(mutedLabel(
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
    progressRunning = true;
    progressBar->setValue(0);
    currentStepLabel->setText(tr("Démonstration"));
    updateStatus(tr("Démonstration de progression — le backend de calcul n'est pas connecté."));
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer, firstStep]() {
        progressBar->setValue(progressBar->value() + 4);
        if (progressBar->value() >= 100) {
            timer->stop();
            timer->deleteLater();
            progressRunning = false;
            currentStepLabel->setText(tr("Terminé"));
            updateStatus(tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
                         .arg(firstStep));
        }
    });
    timer->start(100);
    QTimer::singleShot(150, this, [this, timer]() {
        if (timer->isActive()) {
            auto *cancel = makeButton(tr("Annuler"), this);
            cancel->setObjectName(QStringLiteral("cancelRunButton"));
            statusBar()->addPermanentWidget(cancel);
            connect(cancel, &QPushButton::clicked, this, [this, timer, cancel]() {
                timer->stop();
                timer->deleteLater();
                progressRunning = false;
                currentStepLabel->setText(tr("Annulée"));
                cancel->deleteLater();
                updateStatus(tr("Démonstration annulée."));
            });
            connect(timer, &QTimer::destroyed, cancel, &QPushButton::deleteLater);
        }
    });
}

void MainWindow::showComparison()
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
        comparisonVisible = !comparisonVisible;
        static_cast<ArtworkCanvas *>(canvas)->toggleComparison();
        updateStatus(comparisonVisible ? tr("Comparaison affichée — curseur de séparation actif.")
                                       : tr("Comparaison côte à côte affichée."));
        dialog.accept();
    });
    connect(keepA, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat A conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    connect(keepB, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat B conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::updateStatus(const QString &message)
{
    if (statusLabel)
        statusLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::updateProgress(const QString &stepName, int percentage)
{
    currentStepLabel->setText(stepName);
    progressBar->setValue(qBound(0, percentage, 100));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab) {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
