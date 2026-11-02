#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "projectgrid.h"
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
        chooseReferenceImage();
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
