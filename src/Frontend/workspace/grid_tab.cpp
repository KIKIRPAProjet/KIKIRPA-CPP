/**
 * @brief implémentation de la grille de tuile 
 */
#include "grid_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "workspace_helpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>

GridTab::GridTab(MainWindow *mainWindow, ProjectGrid *projectGrid, ArtworkCanvas *canvas, QWidget *parent)
    : QScrollArea(parent), 
      m_mainWindow(mainWindow), 
      m_projectGrid(projectGrid), 
      m_canvas(canvas)

      {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    m_gridTabPage = new QWidget(this);
    auto *gridLayout = new QVBoxLayout(m_gridTabPage);
    gridLayout->setContentsMargins(7, 8, 7, 8);

    m_gridSummaryLabel = new QLabel;
    gridLayout->addWidget(m_gridSummaryLabel);

    gridLayout->addWidget(createDimensionsGroup());

    m_selectedGridTileLabel = makeMutedLabel(QString(), m_gridTabPage);
    m_selectedGridTileLabel->setWordWrap(true);
    gridLayout->addWidget(m_selectedGridTileLabel);

    auto *tileActions = new QHBoxLayout;
    auto *rotateTile = makeWorkspaceButton(tr("Pivoter 90°"), m_gridTabPage);
    auto *flipTile = makeWorkspaceButton(tr("Retourner"), m_gridTabPage);
    tileActions->addWidget(rotateTile);
    tileActions->addWidget(flipTile);
    gridLayout->addLayout(tileActions);

    m_emptyCellButton = makeWorkspaceButton(tr("Marquer une case vide"), m_gridTabPage);
    gridLayout->addWidget(m_emptyCellButton);

    gridLayout->addWidget(createCaptureGroup());

    m_referencePathLabel = makeMutedLabel(tr("Aucune image de référence"), m_gridTabPage);
    m_referencePathLabel->setWordWrap(true);
    gridLayout->addWidget(m_referencePathLabel);

    auto *importReference = makeWorkspaceButton(tr("Importer une image de référence…"), m_gridTabPage);
    auto *compareReference = makeWorkspaceButton(tr("Comparer à l'image de référence"), m_gridTabPage);
    auto *validateGridButton = makeWorkspaceButton(tr("Valider la grille ✓"), m_gridTabPage, true);

    gridLayout->addWidget(importReference);
    gridLayout->addWidget(compareReference);
    gridLayout->addWidget(validateGridButton);
    gridLayout->addStretch();
    
    setWidget(m_gridTabPage);
    setObjectName(QStringLiteral("gridTabScrollArea"));

    setupConnections(rotateTile, flipTile, importReference, compareReference, validateGridButton);
}

/**
 * Construit le groupe de dimensions de la grille et l'ordre de prise de vue.
 */
QWidget *GridTab::createGridDimensionsGroup(QWidget *parent)
{
    QWidget *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *dimensions = new QFormLayout;
    m_gridRowsSpin = new QSpinBox(container);
    m_gridRowsSpin->setRange(1, 20);
    m_gridRowsSpin->setValue(m_projectGrid->state().gridRows);
   
    m_gridColumnsSpin = new QSpinBox(container);
    m_gridColumnsSpin->setRange(1, 20);
    m_gridColumnsSpin->setValue(m_projectGrid->state().gridColumns);

    dimensions->addRow(tr("Lignes"), m_gridRowsSpin);
    dimensions->addRow(tr("Colonnes"), m_gridColumnsSpin);
    layout->addLayout(dimensions);

    auto *orderGroup = new QGroupBox(tr("Ordre de prise de vue"), container);
    auto *orderLayout = new QVBoxLayout(orderGroup);
    m_captureOrderCombo = new QComboBox(orderGroup);
    m_captureOrderCombo->addItems({
        tr("Automatique (par lignes)"),
        tr("Serpentin vertical, bas gauche"),
        tr("Serpentin vertical, haut droite"),
        tr("Serpentin horizontal"),
        tr("Par lignes"),
        tr("Par colonnes")
    });
    orderLayout->addWidget(m_captureOrderCombo);
    layout->addWidget(orderGroup);

    return container;
}

/**
 * construit le groupe gérant les dimensions physiques de la grille, le recouvrement et la modalité de capture.
 */
QWidget *GridTab::createGridCaptureGroup(QWidget *parent)
{
    auto *captureGroup = new QGroupBox(tr("Prise de vue"), parent);
    auto *captureForm = new QFormLayout(captureGroup);

    m_horizontalOverlapSpin = new QSpinBox(captureGroup);
    m_horizontalOverlapSpin->setRange(0, 90);
    m_horizontalOverlapSpin->setSuffix(QStringLiteral(" %"));
    m_horizontalOverlapSpin->setValue(m_projectGrid->state().horizontalOverlap);

    m_verticalOverlapSpin = new QSpinBox(captureGroup);
    m_verticalOverlapSpin->setRange(0, 90);
    m_verticalOverlapSpin->setSuffix(QStringLiteral(" %"));
    m_verticalOverlapSpin->setValue(m_projectGrid->state().verticalOverlap);
    
    m_linkedOverlapCheck = new QCheckBox(tr("Lier les deux valeurs"), captureGroup);
    m_linkedOverlapCheck->setChecked(m_projectGrid->state().overlapsLinked);
    
    captureForm->addRow(tr("Recouvrement horizontal"), m_horizontalOverlapSpin);
    captureForm->addRow(tr("Recouvrement vertical"), m_verticalOverlapSpin);
    captureForm->addRow(QString(), m_linkedOverlapCheck);
    
    m_modalityCombo = new QComboBox(captureGroup);
    m_modalityCombo->addItems({tr("Infrarouge"), tr("Lumière visible"), tr("Ultraviolet"),
                             tr("Radiographie"), tr("Autre")});
    captureForm->addRow(tr("Modalité"), m_modalityCombo);
    
    m_scaleSpin = new QDoubleSpinBox(captureGroup);
    m_scaleSpin->setRange(0.0, 1000000.0);
    m_scaleSpin->setDecimals(6);
    m_scaleSpin->setSingleStep(0.01);
    m_scaleSpin->setSpecialValueText(tr("À renseigner"));
    m_scaleSpin->setSuffix(tr(" mm / pixel"));
    captureForm->addRow(tr("Échelle"), m_scaleSpin);
    
    m_automaticUncertaintyCheck = new QCheckBox(tr("Automatique"), captureGroup);
    m_automaticUncertaintyCheck->setChecked(m_projectGrid->state().automaticUncertainty);
    
    m_uncertaintySpin = new QDoubleSpinBox(captureGroup);
    m_uncertaintySpin->setRange(0.0, 1000000.0);
    m_uncertaintySpin->setDecimals(3);
    m_uncertaintySpin->setSuffix(tr(" px"));
    m_uncertaintySpin->setEnabled(!m_projectGrid->state().automaticUncertainty);
    
    captureForm->addRow(tr("Marge d'incertitude"), m_uncertaintySpin);
    captureForm->addRow(QString(), m_automaticUncertaintyCheck);
    
    return captureGroup;
}

/**
 * Relie les signaux des boutons et des contrôles de la vue de la grille de tuiles aux slots correspondants.
 * @param rotateTile Bouton pour pivoter la tuile sélectionnée.
 * @param flipTile Bouton pour retourner la tuile sélectionnée.
 * @param importReference Bouton pour importer une image de référence.
 * @param compareReference Bouton pour comparer à l'image de référence.
 * @param validateGridButton Bouton pour valider la grille.
 */
void GridTab::setupConnections(QPushButton *rotateTile, QPushButton *flipTile,
                                         QPushButton *importReference, QPushButton *compareReference,
                                         QPushButton *validateGridButton)
{
    connect(m_gridRowsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {m_projectGrid->setGridRows(value);});
    connect(m_gridColumnsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { m_projectGrid->setGridColumns(value);});
    connect(m_captureOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),this, [this](int value) {m_projectGrid->setCaptureOrder(value);});
    connect(m_horizontalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),this, [this](int value) {m_projectGrid->setHorizontalOverlap(value);});
    connect(m_verticalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),this, [this](int value) { m_projectGrid->setVerticalOverlap(value);});
    connect(m_linkedOverlapCheck, &QCheckBox::toggled, this, [this](bool linked) {m_projectGrid->setOverlapsLinked(linked);});
    connect(m_modalityCombo, &QComboBox::currentTextChanged, this, [this](const QString &value) {m_projectGrid->setModality(value);});
    connect(m_scaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),this, [this](double value) {m_projectGrid->setScaleMmPerPixel(value);});
    connect(m_automaticUncertaintyCheck, &QCheckBox::toggled, this, [this](bool automatic) {m_projectGrid->setAutomaticUncertainty(automatic);});
    connect(m_uncertaintySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {m_projectGrid->setUncertaintyMargin(value);});

    connect(rotateTile, &QPushButton::clicked, m_projectGrid, &ProjectGrid::rotateSelectedTile);
    connect(flipTile, &QPushButton::clicked, m_projectGrid, &ProjectGrid::flipSelectedTile);
    connect(m_emptyCellButton, &QPushButton::clicked, m_projectGrid, &ProjectGrid::toggleSelectedCellEmpty);
    connect(importReference, &QPushButton::clicked, this, [this]() { m_mainWindow->chooseReferenceImage();});
    connect(compareReference, &QPushButton::clicked,m_projectGrid, &ProjectGrid::compareReferenceImage);
    connect(validateGridButton, &QPushButton::clicked,m_projectGrid, &ProjectGrid::validateGrid);

    static_cast<ArtworkCanvas *>(m_canvas)->setGridCallbacks(
        [this](int cell) { m_projectGrid->setSelectedGridCell(cell);},
        [this](int first, int second) { m_projectGrid->swapGridTiles(first, second); });
}

/**
 * Met à jour les paramètres de la grille dans l'interface utilisateur.
 * @param gridState L'état de la grille.
 */
void GridTab::updateParameters(const ProjectGrid::State &gridState)
{
    if (m_gridRowsSpin) {
    const QSignalBlocker blocker(m_gridRowsSpin);
    m_gridRowsSpin->setValue(gridState.gridRows);
    }
    if (m_gridColumnsSpin) {
    const QSignalBlocker blocker(m_gridColumnsSpin);
    m_gridColumnsSpin->setValue(gridState.gridColumns);
    }
    if (m_captureOrderCombo) {
    const QSignalBlocker blocker(m_captureOrderCombo);
    m_captureOrderCombo->setCurrentIndex(gridState.captureOrder);
    }
    if (m_arrangementCombo) {
    const QSignalBlocker blocker(m_arrangementCombo);
    m_arrangementCombo->setCurrentIndex(gridState.captureOrder);
    }
    if (m_horizontalOverlapSpin) {
    const QSignalBlocker blocker(m_horizontalOverlapSpin);
    m_horizontalOverlapSpin->setValue(gridState.horizontalOverlap);
    }
    if (m_verticalOverlapSpin) {
    const QSignalBlocker blocker(m_verticalOverlapSpin);
    m_verticalOverlapSpin->setValue(gridState.verticalOverlap);
    }
    if (m_linkedOverlapCheck) {
    const QSignalBlocker blocker(m_linkedOverlapCheck);
    m_linkedOverlapCheck->setChecked(gridState.overlapsLinked);
    }
    if (m_modalityCombo) {
    const QSignalBlocker blocker(m_modalityCombo);
    const int index = m_modalityCombo->findText(gridState.modality);
        m_modalityCombo->setCurrentIndex(
            index >= 0 ? index : m_modalityCombo->findText(tr("Autre")));
        if (index < 0)
            m_projectGrid->setModality(m_modalityCombo->currentText());
    }
    if (m_scaleSpin) {
    const QSignalBlocker blocker(m_scaleSpin);
    m_scaleSpin->setValue(gridState.scaleMmPerPixel);
    }
    if (m_automaticUncertaintyCheck) {
    const QSignalBlocker blocker(m_automaticUncertaintyCheck);
    m_automaticUncertaintyCheck->setChecked(gridState.automaticUncertainty);
    }
    if (m_uncertaintySpin) {
    const QSignalBlocker blocker(m_uncertaintySpin);
    m_uncertaintySpin->setValue(gridState.uncertaintyMargin);
    m_uncertaintySpin->setEnabled(!gridState.automaticUncertainty);
    }
}

/**
 * Met à jour le résumé de la grille dans l'interface utilisateur.
 * @param gridState L'état de la grille.
 */
void GridTab::updateSummary(const ProjectGrid::State &gridState)
{
    int activeTiles = 0;
    for (int index = 0;
         index < qMin(gridState.gridRows * gridState.gridColumns,
                      static_cast<int>(gridState.gridTiles.size())); ++index) {
        const GridTile &tile = gridState.gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTiles;
    }
    
    const QString validationStatus = gridState.gridValidated ? tr(" · grille validée") : QString();
    m_gridSummaryLabel->setText(tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                .arg(activeTiles).arg(gridState.gridRows).arg(gridState.gridColumns).arg(validationStatus));
   
    const int row = gridState.selectedGridCell / qMax(1, gridState.gridColumns);
    const int column = gridState.selectedGridCell % qMax(1, gridState.gridColumns);
    const GridTile tile = gridState.selectedGridCell >= 0 && gridState.selectedGridCell < gridState.gridTiles.size()
            ? gridState.gridTiles.at(gridState.selectedGridCell) : GridTile{};
    const QString label = QStringLiteral("r%1_c%2").arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));
    
    if (tile.path.isEmpty()) {
         m_selectedGridTileLabel->setText(tile.empty ? tr("Case sélectionnée : %1 (vide)").arg(label) : tr("Case sélectionnée : %1").arg(label));
        } else {
            m_selectedGridTileLabel->setText(
                tr("Sélection : %1 · %2 · %3° · %4")
                    .arg(label, QFileInfo(tile.path).fileName())
                    .arg(tile.rotation)
                    .arg(tile.empty ? tr("vide") : (tile.flipped ? tr("retournée") : tr("normale"))));
        }
    
    
    const bool canRestore = gridState.selectedGridCell >= 0 && gridState.selectedGridCell < gridState.gridTiles.size() && gridState.gridTiles.at(gridState.selectedGridCell).empty;
    m_emptyCellButton->setText(canRestore ? tr("Restaurer la tuile") : tr("Marquer une case vide"));
    
   
    m_referencePathLabel->setText(gridState.referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1").arg(QFileInfo(gridState.referenceImagePath).fileName()));
}

void GridTab::updateState(const ProjectGrid::State &gridState)
{
    updateParameters(gridState);
    updateSummary(gridState);
}