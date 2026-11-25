/**
 * @brief implémentation de la grille de tuile
 */
#include "grid_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "pipeline_runner.h"

#include "core/io/pyramid_cache.h"
#include "core/io/unicode_path.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <QLocale>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QFileInfo>
#include <QSignalBlocker>

namespace {
constexpr int kMaxGridSize = 20;
constexpr int kMaxOverlapPercent = 90;
constexpr double kMaxUncertainty = 1000000.0;
constexpr int kUncertaintyDecimals = 3;

// Met à jour un contrôle sans déclencher ses signaux (évite les mises à jour récursives).
template <typename Widget, typename Setter, typename Value>
void setSilently(Widget *widget, Setter setter, const Value &value)
{
    if (!widget)
        return;
    const QSignalBlocker blocker(widget);
    (widget->*setter)(value);
}
}

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

    gridLayout->addWidget(createGridDimensionsGroup(m_gridTabPage));

    m_selectedGridTileLabel = FrontendUtils::mutedLabel(QString(), m_gridTabPage);
    m_selectedGridTileLabel->setWordWrap(true);
    gridLayout->addWidget(m_selectedGridTileLabel);

    createTileActions(gridLayout);

    gridLayout->addWidget(createGridCaptureGroup(m_gridTabPage));

    createReferenceActions(gridLayout);
    createCacheActions(gridLayout);
    gridLayout->addStretch();

    setWidget(m_gridTabPage);
    setObjectName(QStringLiteral("gridTabScrollArea"));

    setupConnections();
}

/**
 * Construit les boutons d'action sur la tuile sélectionnée (pivoter, retourner, case vide).
 */
void GridTab::createTileActions(QVBoxLayout *layout)
{
    auto *tileActions = new QHBoxLayout;
    m_rotateTileButton = FrontendUtils::makeButton(tr("Pivoter 90°"), m_gridTabPage);
    m_flipTileButton = FrontendUtils::makeButton(tr("Retourner"), m_gridTabPage);
    tileActions->addWidget(m_rotateTileButton);
    tileActions->addWidget(m_flipTileButton);
    layout->addLayout(tileActions);

    m_emptyCellButton = FrontendUtils::makeButton(tr("Marquer une case vide"), m_gridTabPage);
    layout->addWidget(m_emptyCellButton);
}

/**
 * Construit l'étiquette et les boutons liés à l'image de référence et à la validation.
 */
void GridTab::createReferenceActions(QVBoxLayout *layout)
{
    m_referencePathLabel = FrontendUtils::mutedLabel(tr("Aucune image de référence"), m_gridTabPage);
    m_referencePathLabel->setWordWrap(true);
    layout->addWidget(m_referencePathLabel);

    m_importReferenceButton = FrontendUtils::makeButton(tr("Importer une image de référence…"), m_gridTabPage);
    m_compareReferenceButton = FrontendUtils::makeButton(tr("Comparer à l'image de référence"), m_gridTabPage);
    m_validateGridButton = FrontendUtils::makeButton(tr("Valider la grille"), m_gridTabPage, true);

    layout->addWidget(m_importReferenceButton);
    layout->addWidget(m_compareReferenceButton);
    layout->addWidget(m_validateGridButton);
}

/**
 * Construit le bouton qui vide le cache des pyramides du projet.
 */
void GridTab::createCacheActions(QVBoxLayout *layout)
{
    m_freeCacheButton = FrontendUtils::makeButton(tr("Libérer l'espace du cache"), m_gridTabPage);
    layout->addWidget(m_freeCacheButton);
}

/**
 * Vide le cache des pyramides du projet, après avoir annoncé la place récupérée et demandé confirmation.
 * Les pyramides sont des données dérivées : elles sont reconstruites au prochain calcul ou à la prochaine
 * ouverture du projet. Rien n'est supprimé pendant un calcul : ce serait provoquer une course avec le pipeline,
 * qui lit ces fichiers.
 */
bool GridTab::warnIfPipelineRunning()
{
    if (m_mainWindow->pipelineRunner == nullptr || !m_mainWindow->pipelineRunner->isRunning())
        return false;
    QMessageBox::information(this, tr("Calcul en cours"),
                             tr("Attendez la fin du calcul avant de supprimer le cache."));
    return true;
}

void GridTab::updateCacheButtonState()
{
    m_freeCacheButton->setEnabled(!(m_mainWindow->pipelineRunner != nullptr &&
                                    m_mainWindow->pipelineRunner->isRunning()) &&
                                  !m_projectGrid->state().tileDirectory.isEmpty());
}

void GridTab::freePyramidCache()
{
    // Le bouton grisé ne suffit pas : c'est de la signalisation, pas une garantie. L'état du bouton peut
    // dater, et un raccourci clavier ou un script de test appelle cette fonction directement.
    if (warnIfPipelineRunning()) return;

    const QString directory = m_projectGrid->state().tileDirectory;
    if (directory.isEmpty()) {
        QMessageBox::information(this, tr("Libérer l'espace"), tr("Aucun projet ouvert."));
        return;
    }
    const std::filesystem::path cacheDirectory =
        IONs::pyramidCacheDirectory(IONs::pathFromUtf8(directory.toStdString()));

    // La place récupérée est annoncée avant de demander : l'utilisateur doit savoir ce qu'il échange.
    std::uintmax_t total = 0;
    std::error_code errorCode;
    for (const auto &entry : std::filesystem::directory_iterator(cacheDirectory, errorCode))
        if (entry.is_regular_file(errorCode))
            total += entry.file_size(errorCode);
    if (total == 0) {
        QMessageBox::information(this, tr("Libérer l'espace"), tr("Le cache est déjà vide."));
        return;
    }

    const QString question = tr("Supprimer %1 de pyramides en cache ? Elles seront reconstruites au prochain "
                                "calcul ou à la prochaine ouverture du projet.")
                                 .arg(QLocale().formattedDataSize(static_cast<qint64>(total)));
    if (QMessageBox::question(this, tr("Libérer l'espace"), question) != QMessageBox::Yes)
        return;

    // Encore une fois, et c'est celle qui compte : la boîte de confirmation fait tourner la boucle
    // d'événements, donc un calcul a pu démarrer pendant que la question était à l'écran.
    if (warnIfPipelineRunning()) return;

    const std::size_t removed = IONs::removeAllPyramids(cacheDirectory);
    QMessageBox::information(this, tr("Libérer l'espace"),
                             tr("%n fichier(s) supprimé(s) du cache.", "", static_cast<int>(removed)));
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
    m_gridRowsSpin->setRange(1, kMaxGridSize);
    m_gridRowsSpin->setValue(m_projectGrid->state().gridRows);

    m_gridColumnsSpin = new QSpinBox(container);
    m_gridColumnsSpin->setRange(1, kMaxGridSize);
    m_gridColumnsSpin->setValue(m_projectGrid->state().gridColumns);

    dimensions->addRow(tr("Lignes"), m_gridRowsSpin);
    dimensions->addRow(tr("Colonnes"), m_gridColumnsSpin);
    layout->addLayout(dimensions);

    return container;
}

/**
 * construit le groupe gérant les dimensions physiques de la grille, le recouvrement et l'incertitude.
 */
QWidget *GridTab::createGridCaptureGroup(QWidget *parent)
{
    auto *captureGroup = new QGroupBox(tr("Prise de vue"), parent);
    auto *groupLayout = new QVBoxLayout(captureGroup);
    auto *captureForm = new QFormLayout;

    m_horizontalOverlapSpin = new QSpinBox(captureGroup);
    m_horizontalOverlapSpin->setRange(0, kMaxOverlapPercent);
    m_horizontalOverlapSpin->setSuffix(QStringLiteral(" %"));
    m_horizontalOverlapSpin->setValue(m_projectGrid->state().horizontalOverlap);

    m_verticalOverlapSpin = new QSpinBox(captureGroup);
    m_verticalOverlapSpin->setRange(0, kMaxOverlapPercent);
    m_verticalOverlapSpin->setSuffix(QStringLiteral(" %"));
    m_verticalOverlapSpin->setValue(m_projectGrid->state().verticalOverlap);

    m_linkedOverlapCheck = new QCheckBox(tr("Lier les deux valeurs"), captureGroup);
    m_linkedOverlapCheck->setChecked(m_projectGrid->state().overlapsLinked);

    captureForm->addRow(tr("Recouvrement horizontal"), m_horizontalOverlapSpin);
    captureForm->addRow(tr("Recouvrement vertical"), m_verticalOverlapSpin);
    captureForm->addRow(QString(), m_linkedOverlapCheck);
    groupLayout->addLayout(captureForm);

    auto *advancedToggle = new QPushButton(tr("Afficher les paramètres avancés"), captureGroup);
    advancedToggle->setCheckable(true);
    advancedToggle->setFlat(true);
    advancedToggle->setStyleSheet(QStringLiteral("text-align:left; border:0; background:transparent; color:#9bb6d0; padding:2px 0;"));
    groupLayout->addWidget(advancedToggle);

    auto *advancedWidget = new QWidget(captureGroup);
    advancedWidget->setVisible(false);
    auto *advancedForm = new QFormLayout(advancedWidget);
    advancedForm->setContentsMargins(0, 4, 0, 0);

    m_automaticUncertaintyCheck = new QCheckBox(tr("Automatique"), advancedWidget);
    m_automaticUncertaintyCheck->setChecked(m_projectGrid->state().automaticUncertainty);

    m_uncertaintySpin = new QDoubleSpinBox(advancedWidget);
    m_uncertaintySpin->setRange(0.0, kMaxUncertainty);
    m_uncertaintySpin->setDecimals(kUncertaintyDecimals);
    m_uncertaintySpin->setSuffix(tr(" px"));
    m_uncertaintySpin->setEnabled(!m_projectGrid->state().automaticUncertainty);

    advancedForm->addRow(tr("Marge d'incertitude"), m_uncertaintySpin);
    advancedForm->addRow(QString(), m_automaticUncertaintyCheck);
    groupLayout->addWidget(advancedWidget);

    connect(advancedToggle, &QPushButton::toggled, advancedWidget, [advancedToggle, advancedWidget](bool checked) {
        advancedWidget->setVisible(checked);
        advancedToggle->setText(checked ? tr("Masquer les paramètres avancés") : tr("Afficher les paramètres avancés"));
    });

    return captureGroup;
}

/**
 * Relie les signaux des boutons et des contrôles de la vue de la grille de tuiles aux slots correspondants.
 */
void GridTab::setupConnections()
{
    // Dimensions de la grille
    connect(m_gridRowsSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int value) { m_projectGrid->setGridRows(value); });
    connect(m_gridColumnsSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int value) { m_projectGrid->setGridColumns(value); });
    // Recouvrement
    connect(m_horizontalOverlapSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int value) { m_projectGrid->setHorizontalOverlap(value); });
    connect(m_verticalOverlapSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int value) { m_projectGrid->setVerticalOverlap(value); });
    connect(m_linkedOverlapCheck, &QCheckBox::toggled, this,
            [this](bool linked) { m_projectGrid->setOverlapsLinked(linked); });

    // Incertitude
    connect(m_automaticUncertaintyCheck, &QCheckBox::toggled, this,
            [this](bool automatic) { m_projectGrid->setAutomaticUncertainty(automatic); });
    connect(m_uncertaintySpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [this](double value) { m_projectGrid->setUncertaintyMargin(value); });

    // Actions sur la tuile et la grille
    connect(m_rotateTileButton, &QPushButton::clicked,
            m_projectGrid, &ProjectGrid::rotateSelectedTile);
    connect(m_flipTileButton, &QPushButton::clicked,
            m_projectGrid, &ProjectGrid::flipSelectedTile);
    connect(m_emptyCellButton, &QPushButton::clicked,
            m_projectGrid, &ProjectGrid::toggleSelectedCellEmpty);
    connect(m_importReferenceButton, &QPushButton::clicked, this,
            [this]() { m_mainWindow->chooseReferenceImage(); });
    connect(m_compareReferenceButton, &QPushButton::clicked,
            m_projectGrid, &ProjectGrid::compareReferenceImage);
    connect(m_validateGridButton, &QPushButton::clicked,
            m_projectGrid, &ProjectGrid::validateGrid);
    connect(m_freeCacheButton, &QPushButton::clicked, this, &GridTab::freePyramidCache);

    // L'état de la grille ne change pas quand un calcul démarre : sans ces quatre signaux, le bouton
    // resterait actif pendant tout le run.
    if (m_mainWindow->pipelineRunner != nullptr) {
        for (void (PipelineRunnerClass::*signal)() :
             {&PipelineRunnerClass::runStarted, &PipelineRunnerClass::runFinished,
              &PipelineRunnerClass::runCancelled})
            connect(m_mainWindow->pipelineRunner, signal, this, &GridTab::updateCacheButtonState);
        connect(m_mainWindow->pipelineRunner, &PipelineRunnerClass::pausedStateChanged, this,
                [this](bool) { updateCacheButtonState(); });
    }

    m_canvas->setGridCallbacks(
        [this](int cell) { m_projectGrid->setSelectedGridCell(cell); },
        [this](int first, int second) { m_projectGrid->swapGridTiles(first, second); });
}

/**
 * Met à jour les paramètres de la grille dans l'interface utilisateur.
 * @param gridState L'état de la grille.
 */
void GridTab::updateParameters(const ProjectGrid::State &gridState)
{
    setSilently(m_gridRowsSpin, &QSpinBox::setValue, gridState.gridRows);
    setSilently(m_gridColumnsSpin, &QSpinBox::setValue, gridState.gridColumns);
    setSilently(m_horizontalOverlapSpin, &QSpinBox::setValue, gridState.horizontalOverlap);
    setSilently(m_verticalOverlapSpin, &QSpinBox::setValue, gridState.verticalOverlap);
    setSilently(m_linkedOverlapCheck, &QCheckBox::setChecked, gridState.overlapsLinked);
    setSilently(m_automaticUncertaintyCheck, &QCheckBox::setChecked, gridState.automaticUncertainty);
    setSilently(m_uncertaintySpin, &QDoubleSpinBox::setValue, gridState.uncertaintyMargin);

    if (m_uncertaintySpin)
        m_uncertaintySpin->setEnabled(!gridState.automaticUncertainty);
}

/**
 * Met à jour le résumé de la grille dans l'interface utilisateur.
 * @param gridState L'état de la grille.
 */
void GridTab::updateSummary(const ProjectGrid::State &gridState)
{
    int activeTiles = 0;
    const int visibleCells = std::min(gridState.gridRows * gridState.gridColumns,
                                  static_cast<int>(gridState.gridTiles.size()));
    for (int index = 0; index < visibleCells; ++index) {
        const GridTile &tile = gridState.gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTiles;
    }

    const QString validationStatus = gridState.gridValidated ? tr(" · grille validée") : QString();
    m_gridSummaryLabel->setText(tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                .arg(activeTiles).arg(gridState.gridRows).arg(gridState.gridColumns).arg(validationStatus));

    updateSelectionLabel(gridState);
    updateReferenceLabel(gridState);
}

/**
 * Met à jour l'étiquette de la case sélectionnée et le texte du bouton « case vide ».
 */
void GridTab::updateSelectionLabel(const ProjectGrid::State &gridState)
{
    const int selected = gridState.selectedGridCell;
    const bool hasSelection = selected >= 0 && selected < gridState.gridTiles.size();
    const int row = selected / std::max(1, gridState.gridColumns);
    const int column = selected % std::max(1, gridState.gridColumns);
    const GridTile tile = hasSelection ? gridState.gridTiles.at(selected) : GridTile{};
    const QString label = QStringLiteral("r%1_c%2")
            .arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));

    if (tile.path.isEmpty()) {
        m_selectedGridTileLabel->setText(
            tile.empty ? tr("Case sélectionnée : %1 (vide)").arg(label)
                       : tr("Case sélectionnée : %1").arg(label));
    } else {
        const QString state = tile.empty ? tr("vide")
                                         : (tile.flipped ? tr("retournée") : tr("normale"));
        m_selectedGridTileLabel->setText(
            tr("Sélection : %1 · %2 · %3° · %4")
                .arg(label, QFileInfo(tile.path).fileName())
                .arg(tile.rotation)
                .arg(state));
    }

    const bool canRestore = hasSelection && tile.empty;
    m_emptyCellButton->setText(canRestore ? tr("Restaurer la tuile") : tr("Marquer une case vide"));
}

/**
 * Met à jour l'étiquette de l'image de référence.
 */
void GridTab::updateReferenceLabel(const ProjectGrid::State &gridState)
{
    m_referencePathLabel->setText(gridState.referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1").arg(QFileInfo(gridState.referenceImagePath).fileName()));
}

void GridTab::updateState(const ProjectGrid::State &gridState)
{
    // Pas de suppression pendant un calcul : le pipeline lit ces mêmes fichiers.
    updateCacheButtonState();

    updateParameters(gridState);
    updateSummary(gridState);
}
