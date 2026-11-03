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
      mainWindow(mainWindow),
      projectGrid(projectGrid),
      simulationRunner(simulationRunner),
      homePage(homePage),
      pages(new QStackedWidget(mainWindow))
{
    pages->addWidget(homePage);
    createWorkspace();
    mainWindow->setCentralWidget(pages);
}

void WorkspaceUI::updateFromGridState(const QString &projectName,
                                      const ProjectGrid::State &gridState)
{
    if (projectNameLabel)
        projectNameLabel->setText(projectName);
    if (projectNameEdit)
        projectNameEdit->setText(projectName);
    if (projectFolderLabel) {
        projectFolderLabel->setText(gridState.tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(gridState.tileDirectory));
    }
    if (projectFormatLabel) {
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
    if (tileCountLabel) {
        tileCountLabel->setText(
            tr("%1 tuile(s) chargée(s)").arg(gridState.tilePaths.size()));
    }

    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas)) {
        canvasView->setImagePaths(gridState.tilePaths);
        canvasView->setGridState(gridState.gridRows, gridState.gridColumns,
                                 gridState.captureOrder, gridState.gridTiles,
                                 gridState.selectedGridCell);
    }

    if (gridRowsSpin) {
        const QSignalBlocker blocker(gridRowsSpin);
        gridRowsSpin->setValue(gridState.gridRows);
    }
    if (gridColumnsSpin) {
        const QSignalBlocker blocker(gridColumnsSpin);
        gridColumnsSpin->setValue(gridState.gridColumns);
    }
    if (captureOrderCombo) {
        const QSignalBlocker blocker(captureOrderCombo);
        captureOrderCombo->setCurrentIndex(gridState.captureOrder);
    }
    if (arrangementCombo) {
        const QSignalBlocker blocker(arrangementCombo);
        arrangementCombo->setCurrentIndex(gridState.captureOrder);
    }
    if (horizontalOverlapSpin) {
        const QSignalBlocker blocker(horizontalOverlapSpin);
        horizontalOverlapSpin->setValue(gridState.horizontalOverlap);
    }
    if (verticalOverlapSpin) {
        const QSignalBlocker blocker(verticalOverlapSpin);
        verticalOverlapSpin->setValue(gridState.verticalOverlap);
    }
    if (linkedOverlapCheck) {
        const QSignalBlocker blocker(linkedOverlapCheck);
        linkedOverlapCheck->setChecked(gridState.overlapsLinked);
    }
    if (modalityCombo) {
        const QSignalBlocker blocker(modalityCombo);
        const int index = modalityCombo->findText(gridState.modality);
        modalityCombo->setCurrentIndex(
            index >= 0 ? index : modalityCombo->findText(tr("Autre")));
        if (index < 0)
            projectGrid->setModality(modalityCombo->currentText());
    }
    if (scaleSpin) {
        const QSignalBlocker blocker(scaleSpin);
        scaleSpin->setValue(gridState.scaleMmPerPixel);
    }
    if (automaticUncertaintyCheck) {
        const QSignalBlocker blocker(automaticUncertaintyCheck);
        automaticUncertaintyCheck->setChecked(gridState.automaticUncertainty);
    }
    if (uncertaintySpin) {
        const QSignalBlocker blocker(uncertaintySpin);
        uncertaintySpin->setValue(gridState.uncertaintyMargin);
        uncertaintySpin->setEnabled(!gridState.automaticUncertainty);
    }

    const QString scaleText = gridState.scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(gridState.scaleMmPerPixel, 0, 'g', 8)
        : tr("à renseigner");
    if (auto *metadata = workspace->findChild<QLabel *>(
            QStringLiteral("projectMetadataLabel"))) {
        metadata->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                              .arg(gridState.modality, scaleText)
                              .arg(gridState.horizontalOverlap).arg(gridState.verticalOverlap));
    }
    if (auto *title = workspace->findChild<QLabel *>(QStringLiteral("workspaceTitle"))) {
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
    if (gridSummaryLabel) {
        const QString validationStatus =
            gridState.gridValidated ? tr(" · grille validée") : QString();
        gridSummaryLabel->setText(
            tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                .arg(activeTiles).arg(gridState.gridRows).arg(gridState.gridColumns)
                .arg(validationStatus));
    }
    if (selectedGridTileLabel) {
        const int row = gridState.selectedGridCell / qMax(1, gridState.gridColumns);
        const int column = gridState.selectedGridCell % qMax(1, gridState.gridColumns);
        const GridTile tile = gridState.selectedGridCell >= 0 &&
                gridState.selectedGridCell < gridState.gridTiles.size()
            ? gridState.gridTiles.at(gridState.selectedGridCell) : GridTile{};
        const QString label = QStringLiteral("r%1_c%2")
            .arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));
        if (tile.path.isEmpty()) {
            selectedGridTileLabel->setText(tile.empty
                ? tr("Case sélectionnée : %1 (vide)").arg(label)
                : tr("Case sélectionnée : %1").arg(label));
        } else {
            selectedGridTileLabel->setText(
                tr("Sélection : %1 · %2 · %3° · %4")
                    .arg(label, QFileInfo(tile.path).fileName())
                    .arg(tile.rotation)
                    .arg(tile.empty ? tr("vide")
                                    : (tile.flipped ? tr("retournée") : tr("normale"))));
        }
    }
    if (emptyCellButton) {
        const bool canRestore = gridState.selectedGridCell >= 0 &&
                gridState.selectedGridCell < gridState.gridTiles.size()
            && gridState.gridTiles.at(gridState.selectedGridCell).empty;
        emptyCellButton->setText(canRestore ? tr("Restaurer la tuile")
                                              : tr("Marquer une case vide"));
    }
    if (referencePathLabel) {
        referencePathLabel->setText(gridState.referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1")
                  .arg(QFileInfo(gridState.referenceImagePath).fileName()));
        referencePathLabel->setToolTip(gridState.referenceImagePath);
    }
    mainWindow->setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}


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
    projectInfoPanel = new QWidget(center);
    auto *projectInfoLayout = new QVBoxLayout(projectInfoPanel);
    projectInfoLayout->setContentsMargins(32, 32, 32, 32);
    projectInfoLayout->setSpacing(14);
    auto *projectInfoTitle = new QLabel(tr("Informations du projet"), projectInfoPanel);
    projectInfoTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
    projectInfoLayout->addWidget(projectInfoTitle);

    auto *projectNameForm = new QFormLayout;
    projectNameEdit = new QLineEdit(projectInfoPanel);
    projectNameEdit->setObjectName(QStringLiteral("projectNameEdit"));
    auto *renameProject = makeWorkspaceButton(tr("Renommer"), projectInfoPanel);
    auto *projectNameRow = new QHBoxLayout;
    projectNameRow->addWidget(projectNameEdit, 1);
    projectNameRow->addWidget(renameProject);
    projectNameForm->addRow(tr("Nom du projet"), projectNameRow);
    connect(renameProject, &QPushButton::clicked, this, [this]() {
        const QString newName = projectNameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(mainWindow, tr("Nom invalide"),
                tr("Le nom du projet ne peut pas être vide."));
            return;
        }
        mainWindow->projectName = newName;
        mainWindow->refreshProjectInfo();
        mainWindow->updateStatus(
            tr("Projet renommé : %1").arg(mainWindow->projectName));
    });
    projectInfoLayout->addLayout(projectNameForm);

    projectFolderLabel = makeMutedLabel(QString(), projectInfoPanel);
    projectFolderLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFolderLabel);
    projectFormatLabel = new QLabel(projectInfoPanel);
    projectFormatLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFormatLabel);
    projectInfoLayout->addStretch();
    static_cast<QVBoxLayout *>(center->layout())->addWidget(projectInfoPanel, 1);

}


QWidget *WorkspaceUI::createTilesMode()
{
    auto *gridScroll = new QScrollArea;
    gridScroll->setWidgetResizable(true);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridTabPage = new QWidget;
    auto *gridLayout = new QVBoxLayout(gridTabPage);
    gridLayout->setContentsMargins(7, 8, 7, 8);
    gridSummaryLabel = new QLabel;
    gridLayout->addWidget(gridSummaryLabel);

    auto *dimensions = new QFormLayout;
    gridRowsSpin = new QSpinBox(gridTabPage);
    gridRowsSpin->setRange(1, 20);
    gridRowsSpin->setValue(projectGrid->state().gridRows);
    gridRowsSpin->setObjectName(QStringLiteral("gridRowsSpin"));
    gridColumnsSpin = new QSpinBox(gridTabPage);
    gridColumnsSpin->setRange(1, 20);
    gridColumnsSpin->setValue(projectGrid->state().gridColumns);
    gridColumnsSpin->setObjectName(QStringLiteral("gridColumnsSpin"));
    dimensions->addRow(tr("Lignes"), gridRowsSpin);
    dimensions->addRow(tr("Colonnes"), gridColumnsSpin);
    gridLayout->addLayout(dimensions);

    auto *orderGroup = new QGroupBox(tr("Ordre de prise de vue"), gridTabPage);
    auto *orderLayout = new QVBoxLayout(orderGroup);
    captureOrderCombo = new QComboBox(orderGroup);
    captureOrderCombo->addItems({
        tr("Automatique (par lignes)"),
        tr("Serpentin vertical, bas gauche"),
        tr("Serpentin vertical, haut droite"),
        tr("Serpentin horizontal"),
        tr("Par lignes"),
        tr("Par colonnes")
    });
    orderLayout->addWidget(captureOrderCombo);
    gridLayout->addWidget(orderGroup);

    selectedGridTileLabel = makeMutedLabel(QString(), gridTabPage);
    selectedGridTileLabel->setWordWrap(true);
    gridLayout->addWidget(selectedGridTileLabel);
    auto *tileActions = new QHBoxLayout;
    auto *rotateTile = makeWorkspaceButton(tr("Pivoter 90°"), gridTabPage);
    auto *flipTile = makeWorkspaceButton(tr("Retourner"), gridTabPage);
    tileActions->addWidget(rotateTile);
    tileActions->addWidget(flipTile);
    gridLayout->addLayout(tileActions);
    emptyCellButton = makeWorkspaceButton(tr("Marquer une case vide"), gridTabPage);
    gridLayout->addWidget(emptyCellButton);

    auto *captureGroup = new QGroupBox(tr("Prise de vue"), gridTabPage);
    auto *captureForm = new QFormLayout(captureGroup);
    horizontalOverlapSpin = new QSpinBox(captureGroup);
    horizontalOverlapSpin->setRange(0, 90);
    horizontalOverlapSpin->setSuffix(QStringLiteral(" %"));
    verticalOverlapSpin = new QSpinBox(captureGroup);
    verticalOverlapSpin->setRange(0, 90);
    verticalOverlapSpin->setSuffix(QStringLiteral(" %"));
    horizontalOverlapSpin->setValue(projectGrid->state().horizontalOverlap);
    verticalOverlapSpin->setValue(projectGrid->state().verticalOverlap);
    linkedOverlapCheck = new QCheckBox(tr("Lier les deux valeurs"), captureGroup);
    linkedOverlapCheck->setChecked(projectGrid->state().overlapsLinked);
    captureForm->addRow(tr("Recouvrement horizontal"), horizontalOverlapSpin);
    captureForm->addRow(tr("Recouvrement vertical"), verticalOverlapSpin);
    captureForm->addRow(QString(), linkedOverlapCheck);
    modalityCombo = new QComboBox(captureGroup);
    modalityCombo->addItems({tr("Infrarouge"), tr("Lumière visible"), tr("Ultraviolet"),
                             tr("Radiographie"), tr("Autre")});
    captureForm->addRow(tr("Modalité"), modalityCombo);
    scaleSpin = new QDoubleSpinBox(captureGroup);
    scaleSpin->setRange(0.0, 1000000.0);
    scaleSpin->setDecimals(6);
    scaleSpin->setSingleStep(0.01);
    scaleSpin->setSpecialValueText(tr("À renseigner"));
    scaleSpin->setSuffix(tr(" mm / pixel"));
    captureForm->addRow(tr("Échelle"), scaleSpin);
    automaticUncertaintyCheck = new QCheckBox(tr("Automatique"), captureGroup);
    automaticUncertaintyCheck->setChecked(projectGrid->state().automaticUncertainty);
    uncertaintySpin = new QDoubleSpinBox(captureGroup);
    uncertaintySpin->setRange(0.0, 1000000.0);
    uncertaintySpin->setDecimals(3);
    uncertaintySpin->setSuffix(tr(" px"));
    uncertaintySpin->setEnabled(!projectGrid->state().automaticUncertainty);
    captureForm->addRow(tr("Marge d'incertitude"), uncertaintySpin);
    captureForm->addRow(QString(), automaticUncertaintyCheck);
    gridLayout->addWidget(captureGroup);

    referencePathLabel = makeMutedLabel(tr("Aucune image de référence"), gridTabPage);
    referencePathLabel->setWordWrap(true);
    gridLayout->addWidget(referencePathLabel);
    auto *importReference = makeWorkspaceButton(tr("Importer une image de référence…"), gridTabPage);
    auto *compareReference = makeWorkspaceButton(tr("Comparer à l'image de référence"), gridTabPage);
    gridLayout->addWidget(importReference);
    gridLayout->addWidget(compareReference);
    auto *validateGridButton = makeWorkspaceButton(tr("Valider la grille ✓"), gridTabPage, true);
    gridLayout->addWidget(validateGridButton);
    gridLayout->addStretch();
    gridScroll->setWidget(gridTabPage);
    gridScroll->setObjectName(QStringLiteral("gridTabScrollArea"));

    connect(gridRowsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        projectGrid->setGridRows(value);
    });
    connect(gridColumnsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        projectGrid->setGridColumns(value);
    });
    connect(captureOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int value) {
        projectGrid->setCaptureOrder(value);
    });
    connect(horizontalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        projectGrid->setHorizontalOverlap(value);
    });
    connect(verticalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        projectGrid->setVerticalOverlap(value);
    });
    connect(linkedOverlapCheck, &QCheckBox::toggled, this, [this](bool linked) {
        projectGrid->setOverlapsLinked(linked);
    });
    connect(modalityCombo, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        projectGrid->setModality(value);
    });
    connect(scaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        projectGrid->setScaleMmPerPixel(value);
    });
    connect(automaticUncertaintyCheck, &QCheckBox::toggled, this, [this](bool automatic) {
        projectGrid->setAutomaticUncertainty(automatic);
    });
    connect(uncertaintySpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        projectGrid->setUncertaintyMargin(value);
    });
    connect(rotateTile, &QPushButton::clicked, projectGrid, &ProjectGrid::rotateSelectedTile);
    connect(flipTile, &QPushButton::clicked, projectGrid, &ProjectGrid::flipSelectedTile);
    connect(emptyCellButton, &QPushButton::clicked,
            projectGrid, &ProjectGrid::toggleSelectedCellEmpty);
    connect(importReference, &QPushButton::clicked, this, [this]() {
        mainWindow->chooseReferenceImage();
    });
    connect(compareReference, &QPushButton::clicked,
            projectGrid, &ProjectGrid::compareReferenceImage);
    connect(validateGridButton, &QPushButton::clicked,
            projectGrid, &ProjectGrid::validateGrid);
    static_cast<ArtworkCanvas *>(canvas)->setGridCallbacks(
        [this](int cell) {
            projectGrid->setSelectedGridCell(cell);
        },
        [this](int first, int second) { projectGrid->swapGridTiles(first, second); });

    return gridScroll;
}


QWidget *WorkspaceUI::createMasksMode()
{
    auto *layersPage = new QWidget;
    auto *layersLayout = new QVBoxLayout(layersPage);
    layersLayout->addWidget(new QLabel(tr("Calques")));
    const QStringList layerNames = {tr("Lignes de couture"), tr("Erreur d'alignement"),
                                    tr("Tuiles"), tr("Mosaïque"), tr("Masques")};
    for (const QString &name : layerNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        layersLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, name](bool checked) {
            mainWindow->updateStatus(tr("Calque « %1 » %2.").arg(name, checked ? tr("affiché") : tr("masqué")));
            if (name == tr("Masques")) {
                mainWindow->maskVisible = checked;
                static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(checked);
            }
        });
    }
    layersLayout->addStretch();
    return layersPage;

}


QWidget *WorkspaceUI::createAlignmentMode()
{
    auto *infoPage = new QWidget;
    auto *infoLayout = new QVBoxLayout(infoPage);
    infoLayout->addWidget(new QLabel(tr("Projet sélectionné")));
    projectNameLabel = new QLabel;
    projectNameLabel->setWordWrap(true);
    infoLayout->addWidget(projectNameLabel);
    tileCountLabel = new QLabel;
    infoLayout->addWidget(tileCountLabel);
    auto *projectMetadata = makeMutedLabel(QString(), infoPage);
    projectMetadata->setObjectName(QStringLiteral("projectMetadataLabel"));
    infoLayout->addWidget(projectMetadata);
    auto *maskButton = makeWorkspaceButton(tr("Détecter les bords noirs"), infoPage);
    infoLayout->addWidget(maskButton);
    connect(maskButton, &QPushButton::clicked, this, [this]() {
        mainWindow->setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        mainWindow->updateStatus(tr("Masque proposé. Vérifiez-le avant validation."));
    });
    auto *pairButton = makeWorkspaceButton(tr("Ouvrir la vue Paire"), infoPage);
    infoLayout->addWidget(pairButton);
    connect(pairButton, &QPushButton::clicked, this, [this]() { mainWindow->setMode(3); });
    auto *validateButton = makeWorkspaceButton(tr("Valider la paire"), infoPage, true);
    infoLayout->addWidget(validateButton);
    connect(validateButton, &QPushButton::clicked, this, [this]() {
        mainWindow->updateStatus(tr("Paire validée par l'opérateur; les étapes suivantes sont à refaire."));
    });
    auto *adjustButton = makeWorkspaceButton(tr("Saisir un décalage…"), infoPage);
    infoLayout->addWidget(adjustButton);
    connect(adjustButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(mainWindow);
        dialog.setWindowTitle(tr("Correction manuelle"));
        QFormLayout form(&dialog);
        auto *dx = new QDoubleSpinBox(&dialog);
        auto *dy = new QDoubleSpinBox(&dialog);
        dx->setRange(-100000, 100000); dx->setDecimals(1); dx->setValue(4065.5);
        dy->setRange(-100000, 100000); dy->setDecimals(1); dy->setValue(-20.7);
        form.addRow(tr("Décalage horizontal (px)"), dx);
        form.addRow(tr("Décalage vertical (px)"), dy);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            mainWindow->updateStatus(tr("Correction enregistrée : dx %1 px, dy %2 px.")
                         .arg(dx->value()).arg(dy->value()));
    });
    infoLayout->addStretch();
    return infoPage;
}


QWidget *WorkspaceUI::createAssemblyMode()
{
    auto *chainPage = new QWidget;
    auto *chainLayout = new QVBoxLayout(chainPage);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));
    const QStringList steps = {tr("1  Import"), tr("2  Masques"), tr("3  Correspondance"),
        tr("4  Optimisation"), tr("5  Calage local"), tr("6  Égalisation"),
        tr("7  Ligne de couture"), tr("8  Fusion et export")};
    auto *stepList = new QListWidget(chainPage);
    for (int i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem((i == 2 ? QStringLiteral("⚠  ") : QStringLiteral("●  "))
                                         + steps.at(i), stepList);
        item->setData(Qt::UserRole, i + 1);
    }
    chainLayout->addWidget(stepList);
    connect(stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });
    auto *relaunch = makeWorkspaceButton(tr("Relancer à partir d'ici"), chainPage);
    chainLayout->addWidget(relaunch);
    connect(relaunch, &QPushButton::clicked, this, [this, stepList]() {
        const int row = qMax(0, stepList->currentRow());
        mainWindow->requestRun(row + 1);
    });
    auto *selectAlgo = new QComboBox(chainPage);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    chainLayout->addWidget(selectAlgo);
    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        mainWindow->updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
    rightTabs->addTab(chainPage, tr("Chaîne"));
    return chainPage;
}



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
        connect(button, &QPushButton::clicked, this, [this, i]() { mainWindow->setMode(i); });
    }
    auto *run = makeWorkspaceButton(tr("▶ Tout lancer"), stages, true);
    stageLayout->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() { mainWindow->requestRun(1); });
    auto *compare = makeWorkspaceButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked,
            simulationRunner, &SimulationRunner::showComparison);
    
    return stages;
}

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

QWidget* WorkspaceUI::createLeftToolBar()
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

QWidget* WorkspaceUI::createCenterArea()
{
    QWidget *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    
    createProjectMode(center);
    
    m_canvas = new ArtworkCanvas(center);
    centerLayout->addWidget(canvas, 1);
    
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
    canvasControlsLayout->addWidget(zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    centerLayout->addWidget(canvasControls);

    m_projectInfoPanel->setVisible(false);

    connect(minus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMax(5, mainWindow->zoomPercent - 5);
        m_zoomLabel->setText(QString::number(mainWindow->zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(mainWindow->zoomPercent);
    });
    connect(plus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMin(1600, m_mainWindow->zoomPercent + 5);
        m_zoomLabel->setText(QString::number(m_mainWindow->zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(m_mainWindow->zoomPercent);
    });
    connect(fit, &QPushButton::clicked, this, [this]() {
        zoomLabel->setText(tr("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(100);
    });
    connect(oneToOne, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = 100;
        m_zoomLabel->setText(QStringLiteral("100 %"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(m_mainWindow->zoomPercent);
    });
    
    return center;
}

QTabWidget *WorkspaceUI::createRightTabs()
{
    QTabWidget *rightTabs = new QTabWidget;
    rightTabs->setMinimumWidth(218);

    m_gridTabPage = createTilesMode();
    rightTabs->addTab(createMasksMode(), tr("Calques"));
    rightTabs->addTab(createAssemblyMode(), tr("Chaîne"));
    rightTabs->addTab(createAlignmentMode(), tr("Infos"));
    
    return rightTabs;
}

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
            pairsTable->setItem(row, col, new QTableWidgetItem(pairRows[row].at(col)));
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

    connect(m_pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { mainWindow->setMode(3); });
    connect(m_pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        m_mainWindow->updateStatus(tr("Paire sélectionnée : %1").arg(m_pairsTable->item(row, 0)->text()));
    });

    return bottomTabs;
}

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