#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "workspace_helpers.h"

#include <QButtonGroup>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
#include <QToolButton>
#include <QVBoxLayout>

QWidget *MainWindow::createTilesMode()
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
    gridRowsSpin->setValue(gridRows);
    gridRowsSpin->setObjectName(QStringLiteral("gridRowsSpin"));
    gridColumnsSpin = new QSpinBox(gridTabPage);
    gridColumnsSpin->setRange(1, 20);
    gridColumnsSpin->setValue(gridColumns);
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
    horizontalOverlapSpin->setValue(horizontalOverlap);
    verticalOverlapSpin->setValue(verticalOverlap);
    linkedOverlapCheck = new QCheckBox(tr("Lier les deux valeurs"), captureGroup);
    linkedOverlapCheck->setChecked(overlapsLinked);
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
    automaticUncertaintyCheck->setChecked(automaticUncertainty);
    uncertaintySpin = new QDoubleSpinBox(captureGroup);
    uncertaintySpin->setRange(0.0, 1000000.0);
    uncertaintySpin->setDecimals(3);
    uncertaintySpin->setSuffix(tr(" px"));
    uncertaintySpin->setEnabled(!automaticUncertainty);
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
    return gridScroll;

    connect(gridRowsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        gridRows = value;
        selectedGridCell = qMin(selectedGridCell, gridRows * gridColumns - 1);
        gridValidated = false;
        refreshGrid();
    });
    connect(gridColumnsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        gridColumns = value;
        selectedGridCell = qMin(selectedGridCell, gridRows * gridColumns - 1);
        gridValidated = false;
        refreshGrid();
    });
    connect(captureOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int value) {
        captureOrder = value;
        gridValidated = false;
        refreshGrid();
    });
    connect(horizontalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        horizontalOverlap = value;
        if (overlapsLinked && verticalOverlapSpin)
            verticalOverlapSpin->setValue(value);
        gridValidated = false;
        refreshGrid();
    });
    connect(verticalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        verticalOverlap = value;
        if (overlapsLinked && horizontalOverlapSpin)
            horizontalOverlapSpin->setValue(value);
        gridValidated = false;
        refreshGrid();
    });
    connect(linkedOverlapCheck, &QCheckBox::toggled, this, [this](bool linked) {
        overlapsLinked = linked;
        if (linked && verticalOverlapSpin)
            verticalOverlapSpin->setValue(horizontalOverlap);
        gridValidated = false;
        refreshGrid();
    });
    connect(modalityCombo, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        modality = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(scaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        scaleMmPerPixel = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(automaticUncertaintyCheck, &QCheckBox::toggled, this, [this](bool automatic) {
        automaticUncertainty = automatic;
        uncertaintySpin->setEnabled(!automatic);
        gridValidated = false;
        refreshGridControls();
    });
    connect(uncertaintySpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        uncertaintyMargin = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(rotateTile, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
            gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
            updateStatus(tr("Sélectionnez d'abord une tuile non vide."));
            return;
        }
        gridTiles[selectedGridCell].rotation = (gridTiles.at(selectedGridCell).rotation + 90) % 360;
        gridValidated = false;
        refreshGrid();
    });
    connect(flipTile, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
            gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
            updateStatus(tr("Sélectionnez d'abord une tuile non vide."));
            return;
        }
        gridTiles[selectedGridCell].flipped = !gridTiles.at(selectedGridCell).flipped;
        gridValidated = false;
        refreshGrid();
    });
    connect(emptyCellButton, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridRows * gridColumns) {
            updateStatus(tr("Sélectionnez une case de la grille."));
            return;
        }
        while (gridTiles.size() <= selectedGridCell)
            gridTiles.append(GridTile{});
        gridTiles[selectedGridCell].empty = !gridTiles.at(selectedGridCell).empty;
        gridValidated = false;
        refreshGrid();
    });
    connect(importReference, &QPushButton::clicked, this, &MainWindow::importReferenceImage);
    connect(compareReference, &QPushButton::clicked, this, &MainWindow::compareReferenceImage);
    connect(validateGridButton, &QPushButton::clicked, this, &MainWindow::validateGrid);
    static_cast<ArtworkCanvas *>(canvas)->setGridCallbacks(
        [this](int cell) {
            selectedGridCell = cell;
            refreshGridControls();
        },
        [this](int first, int second) { swapGridTiles(first, second); });

}
