/*
 * .cpp : le POURQUOI
 * - choix de conception : un tableau pour voir toutes les paires d'un coup et les trier, une fiche pour la paire choisie ;
 *   seules deux actions, celles qui changent le calcul (« Corriger… ») ou son suivi (« Valider ») ;
 * - cas limite : le tableau est reconstruit à chaque mise à jour des paires, signaux bloqués pour ne pas renvoyer de
 *   sélection pendant le remplissage ;
 * - unité ou repère : décalages et écarts en pixels pleine résolution (px) ;
 * - raison non évidente : les termes techniques (confiance, NCC, raison brute du calcul) restent en infobulle pour que
 *   la fiche se lise sans connaître les algorithmes.
 */
/*
 * Header : le QUOI
 * Affiche le panneau latéral « Paire » : tableau des paires, fiche de la paire choisie et ses actions.
 * - ce que ça fait : présente l'état, une explication en clair, le décalage, les points et l'écart après positionnement ;
 * - ce que ça renvoie : émet des signaux lors de la sélection, de la validation ou de la correction manuelle d'une paire ;
 * - quand ça lève une exception : si l'allocation d'un widget Qt échoue.
 */
#include "alignment_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "pipeline_results.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>

namespace {

constexpr int COLUMN_PAIR = 0;
constexpr int COLUMN_POINTS = 1;
constexpr int COLUMN_ERROR = 2;
constexpr int COLUMN_COUNT = 3;
constexpr int PAIR_INDEX_ROLE = Qt::UserRole;
constexpr int SORT_KEY_ROLE = Qt::UserRole + 1;
constexpr int TABLE_MINIMUM_HEIGHT = 180;
constexpr double MAXIMUM_OFFSET_PX = 100000.0;

// Cellule triée sur une valeur (ordre des états, nombre, écart) plutôt que sur son texte.
class SortableItem : public QTableWidgetItem
{
public:
    SortableItem(const QString &text, double sortKey) : QTableWidgetItem(text) { setData(SORT_KEY_ROLE, sortKey); }
    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(SORT_KEY_ROLE).toDouble() < other.data(SORT_KEY_ROLE).toDouble();
    }
};

// Les paires à traiter d'abord : échec, puis à vérifier.
int stateOrder(FrontendUtils::PairStatus status)
{
    switch (status) {
    case FrontendUtils::PairStatus::Failed: return 0;
    case FrontendUtils::PairStatus::Dubious: return 1;
    case FrontendUtils::PairStatus::Validated: return 2;
    case FrontendUtils::PairStatus::Reliable: return 3;
    }
    return 3;
}

QColor stateColor(FrontendUtils::PairStatus status)
{
    switch (status) {
    case FrontendUtils::PairStatus::Failed: return {239, 81, 73};
    case FrontendUtils::PairStatus::Dubious: return {240, 160, 60};
    case FrontendUtils::PairStatus::Validated: return {95, 179, 232};
    case FrontendUtils::PairStatus::Reliable: return {78, 201, 120};
    }
    return {200, 200, 200};
}

bool needsCheck(const FrontendUtils::PairAlignmentInfo &pair)
{
    return pair.status == FrontendUtils::PairStatus::Dubious || pair.status == FrontendUtils::PairStatus::Failed;
}

QString formatPixels(double value)
{
    return QString::number(value, 'f', 1).replace('.', ',');
}

// Longueur du début (ou de la fin, si fromEnd) commun à tous les noms.
int commonLength(const QStringList &names, bool fromEnd)
{
    int length = 0;
    const QString &first = names.first();
    while (length < first.size()) {
        const QChar character = fromEnd ? first.at(first.size() - 1 - length) : first.at(length);
        for (const QString &name : names)
            if (length >= name.size() || (fromEnd ? name.at(name.size() - 1 - length) : name.at(length)) != character)
                return length;
        ++length;
    }
    return length;
}

// Noms de tuiles sans la partie commune à toutes (« 06_no filter f16 » et « 05_no filter f16 » donnent « 06 » et
// « 05 »), pour que le tableau reste lisible dans un panneau étroit. Les noms gardés tels quels si le raccourci en
// viderait un ou en rendrait deux identiques.
QMap<QString, QString> shortTileNames(const QVector<FrontendUtils::PairAlignmentInfo> &pairs)
{
    QStringList names;
    for (const auto &pair : pairs)
        names << pair.tileA << pair.tileB;
    names.removeDuplicates();
    QMap<QString, QString> shortNames;
    if (names.size() < 2)
        return shortNames;
    // Coupe seulement sur un séparateur : « 10_x » et « 11_x » ne doivent pas devenir « 0 » et « 1 ».
    const QString &first = names.first();
    const auto isSeparator = [](QChar character) { return character == '_' || character == '-' || character.isSpace(); };
    int prefix = commonLength(names, false);
    while (prefix > 0 && !isSeparator(first.at(prefix - 1)))
        --prefix;
    int suffix = commonLength(names, true);
    while (suffix > 0 && !isSeparator(first.at(first.size() - suffix)))
        --suffix;
    for (const QString &name : names) {
        QString shortName = name.mid(prefix, name.size() - prefix - suffix);
        while (!shortName.isEmpty() && isSeparator(shortName.back()))
            shortName.chop(1);
        if (shortName.isEmpty() || shortNames.key(shortName, QString()) != QString())
            return {};
        shortNames.insert(name, shortName);
    }
    return shortNames;
}

QLabel *valueLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setWordWrap(true);
    return label;
}

}

AlignmentTab::AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_canvas(canvas)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    createSummary(mainLayout);
    createTable(mainLayout);
    createDetails(mainLayout);
    mainLayout->addStretch();
    setPairs({}, 0);   // les paires arrivent avec le premier calcul (MainWindow::applyPipelineResults)
}

// Résumé des états, filtre « à vérifier » et passage à la prochaine paire à vérifier (touche N).
void AlignmentTab::createSummary(QVBoxLayout *layout)
{
    m_summaryLabel = FrontendUtils::mutedLabel(QString(), this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    auto *filterRow = new QHBoxLayout;
    m_toCheckOnly = new QCheckBox(tr("À vérifier seulement"), this);
    m_toCheckOnly->setToolTip(tr("N'afficher que les paires à vérifier ou en échec"));
    m_nextToCheckButton = FrontendUtils::makeButton(tr("Suivante à vérifier"), this);
    m_nextToCheckButton->setToolTip(tr("Aller à la prochaine paire à vérifier ou en échec (N)"));
    filterRow->addWidget(m_toCheckOnly);
    filterRow->addStretch();
    filterRow->addWidget(m_nextToCheckButton);
    layout->addLayout(filterRow);

    connect(m_toCheckOnly, &QCheckBox::toggled, this, [this](bool) { rebuildTable(); });
    connect(m_nextToCheckButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->setMode(3);
        emit nextDubiousRequested();
    });
}

void AlignmentTab::createTable(QVBoxLayout *layout)
{
    m_table = new QTableWidget(0, COLUMN_COUNT, this);
    m_table->setObjectName(QStringLiteral("pairTable"));
    m_table->setHorizontalHeaderLabels({tr("Paire"), tr("Points"), tr("Écart (px)")});
    m_table->horizontalHeaderItem(COLUMN_ERROR)->setToolTip(tr("Écart restant après le positionnement global"));
    m_table->horizontalHeader()->setSectionResizeMode(COLUMN_PAIR, QHeaderView::Stretch);
    for (const int column : {COLUMN_POINTS, COLUMN_ERROR})
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMinimumHeight(TABLE_MINIMUM_HEIGHT);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(COLUMN_POINTS, Qt::AscendingOrder);   // paires avec le moins de points en premier
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QList<QTableWidgetItem *> selected = m_table->selectedItems();
        if (selected.isEmpty())
            return;
        const int pairIndex = m_table->item(selected.first()->row(), COLUMN_PAIR)->data(PAIR_INDEX_ROLE).toInt();
        if (pairIndex != m_currentPairIndex)
            emit pairSelectionRequested(pairIndex);
    });
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (!item)
            return;
        const int pairIndex = m_table->item(item->row(), COLUMN_PAIR)->data(PAIR_INDEX_ROLE).toInt();
        emit pairIsolationRequested(pairIndex);
    });
}

// Fiche de la paire choisie : état, explication, mesures, puis « Corriger… » et « Valider ».
void AlignmentTab::createDetails(QVBoxLayout *layout)
{
    m_details = new QWidget(this);
    auto *detailsLayout = new QVBoxLayout(m_details);
    detailsLayout->setContentsMargins(0, 4, 0, 0);

    auto *headerRow = new QHBoxLayout;
    m_pairTitleLabel = new QLabel(m_details);
    m_pairTitleLabel->setStyleSheet(QStringLiteral("font-weight:600; color:#ececec;"));
    m_stateLabel = new QLabel(m_details);
    headerRow->addWidget(m_pairTitleLabel, 1);
    headerRow->addWidget(m_stateLabel);
    detailsLayout->addLayout(headerRow);

    m_explanationLabel = new QLabel(m_details);
    m_explanationLabel->setWordWrap(true);
    detailsLayout->addWidget(m_explanationLabel);

    auto *form = new QFormLayout;
    m_offsetValueLabel = valueLabel(m_details);
    m_pointsValueLabel = valueLabel(m_details);
    m_errorValueLabel = valueLabel(m_details);
    m_correctionTitleLabel = FrontendUtils::mutedLabel(tr("Correction"), m_details);
    m_correctionValueLabel = valueLabel(m_details);
    form->addRow(FrontendUtils::mutedLabel(tr("Décalage de B dans A"), m_details), m_offsetValueLabel);
    form->addRow(FrontendUtils::mutedLabel(tr("Points"), m_details), m_pointsValueLabel);
    form->addRow(FrontendUtils::mutedLabel(tr("Écart après positionnement"), m_details), m_errorValueLabel);
    form->addRow(m_correctionTitleLabel, m_correctionValueLabel);
    detailsLayout->addLayout(form);

    m_actionsBar = new QWidget(m_details);
    auto *actions = new QHBoxLayout(m_actionsBar);
    actions->setContentsMargins(0, 0, 0, 0);
    m_correctButton = FrontendUtils::makeButton(tr("Corriger…"), m_actionsBar, true);
    m_correctButton->setToolTip(tr("Placer des paires de points à la main en cliquant sur les deux tuiles"));
    m_validateButton = FrontendUtils::makeButton(tr("Valider"), m_actionsBar);
    m_validateButton->setToolTip(tr("Accepter cette paire telle qu'elle a été mesurée"));
    actions->addWidget(m_correctButton);
    actions->addWidget(m_validateButton);
    detailsLayout->addWidget(m_actionsBar);

    m_pickingBar = new QWidget(m_details);
    auto *pickingLayout = new QVBoxLayout(m_pickingBar);
    pickingLayout->setContentsMargins(0, 4, 0, 0);
    pickingLayout->setSpacing(6);
    m_pickingHintLabel = FrontendUtils::mutedLabel(QString(), m_pickingBar);
    m_pickingHintLabel->setWordWrap(true);
    pickingLayout->addWidget(m_pickingHintLabel);
    auto *pickingButtons = new QHBoxLayout;
    pickingButtons->setContentsMargins(0, 0, 0, 0);
    m_applyPointsButton = FrontendUtils::makeButton(tr("Appliquer"), m_pickingBar, true);
    m_applyPointsButton->setEnabled(false);
    m_undoPointButton = FrontendUtils::makeButton(tr("Retirer"), m_pickingBar);
    m_undoPointButton->setToolTip(tr("Retirer le dernier point placé (clic droit)"));
    m_cancelPickingButton = FrontendUtils::makeButton(tr("Annuler"), m_pickingBar);
    pickingButtons->addWidget(m_applyPointsButton);
    pickingButtons->addWidget(m_undoPointButton);
    pickingButtons->addWidget(m_cancelPickingButton);
    pickingLayout->addLayout(pickingButtons);
    m_pickingBar->setVisible(false);
    detailsLayout->addWidget(m_pickingBar);

    layout->addWidget(m_details);

    connect(m_correctButton, &QPushButton::clicked, this, &AlignmentTab::startInteractiveCorrection);
    connect(m_validateButton, &QPushButton::clicked, this, [this]() { emit pairValidated(m_currentPairIndex); });
    connect(m_undoPointButton, &QPushButton::clicked, this, [this]() {
        if (m_canvas)
            m_canvas->undoLastPickedPoint();
    });
    connect(m_cancelPickingButton, &QPushButton::clicked, this, &AlignmentTab::cancelPointPickingUi);
    connect(m_applyPointsButton, &QPushButton::clicked, this, [this]() {
        if (!m_canvas)
            return;
        const QVector<QPointF> pointsInA = m_canvas->pickedPointsInA();
        const QVector<QPointF> pointsInB = m_canvas->pickedPointsInB();
        cancelPointPickingUi();
        if (!pointsInA.isEmpty() && pointsInA.size() == pointsInB.size())
            emit manualPointsApplied(m_currentPairIndex, pointsInA, pointsInB);
    });
    if (m_canvas) {
        connect(m_canvas, &ArtworkCanvas::manualPointsChanged, this, &AlignmentTab::refreshPickingUi);
        connect(m_canvas, &ArtworkCanvas::pointPickingCancelled, this, &AlignmentTab::cancelPointPickingUi);
    }
}

void AlignmentTab::setPairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs, int currentPairIndex)
{
    const bool hadValidPrevious = !m_pairs.isEmpty() && m_currentPairIndex >= 0 && m_currentPairIndex < m_pairs.size();
    const int nextIndex = pairs.isEmpty() ? 0 : qBound(0, currentPairIndex, static_cast<int>(pairs.size()) - 1);
    const bool sameSelectedPair = hadValidPrevious && !pairs.isEmpty()
        && m_pairs.at(m_currentPairIndex).tileIndexA == pairs.at(nextIndex).tileIndexA
        && m_pairs.at(m_currentPairIndex).tileIndexB == pairs.at(nextIndex).tileIndexB;

    m_pairs = pairs;
    m_shortTileNames = shortTileNames(m_pairs);
    m_currentPairIndex = nextIndex;

    std::array<int, 4> counts{};   // ordre de stateOrder : échec, à vérifier, validée, fiable
    for (const auto &pair : m_pairs)
        ++counts[stateOrder(pair.status)];
    m_summaryLabel->setText(m_pairs.isEmpty()
        ? tr("Lancez l'assemblage pour voir les paires de tuiles et leur état.")
        : tr("%1 paire(s) : %2 fiable(s), %3 à vérifier")
              .arg(m_pairs.size()).arg(counts[3] + counts[2]).arg(counts[0] + counts[1]));
    m_toCheckOnly->setEnabled(!m_pairs.isEmpty());
    m_nextToCheckButton->setEnabled(counts[0] + counts[1] > 0);

    rebuildTable();
    m_details->setVisible(!m_pairs.isEmpty());
    if (!sameSelectedPair) {
        cancelPointPickingUi();
    }
    if (m_pairs.isEmpty())
        return;
    setCurrentPair(m_pairs.at(m_currentPairIndex));
}

// Une ligne par paire (seulement les paires à vérifier si le filtre est coché), puis la paire courante sélectionnée.
void AlignmentTab::rebuildTable()
{
    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    for (const auto &pair : m_pairs) {
        if (m_toCheckOnly->isChecked() && !needsCheck(pair))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        fillRow(row, pair);
    }
    m_table->setSortingEnabled(true);
    selectCurrentRow();
}

void AlignmentTab::fillRow(int row, const FrontendUtils::PairAlignmentInfo &pair)
{
    const PairOperatorText text = FrontendNs::describePairForOperator(pair);
    auto *pairItem = new QTableWidgetItem(shortPairLabel(pair));
    pairItem->setData(PAIR_INDEX_ROLE, pair.pairIndex);
    pairItem->setToolTip(pair.pairLabel() + QStringLiteral("\n") + text.explanation);
    auto *pointsItem = new SortableItem(QString::number(pair.totalMatches), pair.totalMatches);
    const double error = pair.optimizationErrorPx.value_or(-1.0);   // non mesuré : en tête du tri croissant
    auto *errorItem = new SortableItem(pair.optimizationErrorPx ? formatPixels(error) : QStringLiteral("-"), error);
    errorItem->setToolTip(FrontendUtils::formatMeasure(pair.optimizationErrorPx, 2, QStringLiteral(" px")));
    pointsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    errorItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (needsCheck(pair)) {
        const QColor alertColor = stateColor(FrontendUtils::PairStatus::Failed);
        QFont bold = pairItem->font();
        bold.setBold(true);
        pairItem->setFont(bold);
        for (QTableWidgetItem *cell : {pairItem, static_cast<QTableWidgetItem *>(pointsItem),
                                       static_cast<QTableWidgetItem *>(errorItem)}) {
            cell->setForeground(alertColor);
        }
    }
    m_table->setItem(row, COLUMN_PAIR, pairItem);
    m_table->setItem(row, COLUMN_POINTS, pointsItem);
    m_table->setItem(row, COLUMN_ERROR, errorItem);
}

void AlignmentTab::selectCurrentRow()
{
    const QSignalBlocker blocker(m_table);
    m_table->clearSelection();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, COLUMN_PAIR)->data(PAIR_INDEX_ROLE).toInt() == m_currentPairIndex) {
            m_table->selectRow(row);
            m_table->scrollToItem(m_table->item(row, COLUMN_PAIR));
            return;
        }
    }
}

void AlignmentTab::setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair)
{
    if (pair.pairIndex != m_currentPairIndex && m_canvas && m_canvas->isPickingPoints())
        cancelPointPickingUi();
    m_currentPairIndex = pair.pairIndex;
    if (m_currentPairIndex >= 0 && m_currentPairIndex < m_pairs.size())
        m_pairs[m_currentPairIndex] = pair;
    selectCurrentRow();

    const PairOperatorText text = FrontendNs::describePairForOperator(pair);
    const QColor color = stateColor(pair.status);
    m_pairTitleLabel->setText(shortPairLabel(pair));
    m_pairTitleLabel->setToolTip(pair.pairLabel());
    m_stateLabel->setVisible(pair.status != FrontendUtils::PairStatus::Reliable);
    m_stateLabel->setText(text.state);
    m_stateLabel->setStyleSheet(QStringLiteral("color:%1; font-weight:600; border:1px solid %1; border-radius:8px; "
                                               "padding:1px 8px;").arg(color.name()));
    m_explanationLabel->setText(text.explanation);
    m_explanationLabel->setToolTip(text.details);
    m_stateLabel->setToolTip(text.details);

    const bool measured = pair.totalMatches > 0 || !pair.overlapInA.isEmpty();   // échec sans estimation : rien de mesuré
    m_offsetValueLabel->setText(measured ? tr("dx = %1 px ; dy = %2 px").arg(formatPixels(pair.dx), formatPixels(pair.dy))
                                         : tr("non mesuré"));
    m_pointsValueLabel->setText(tr("%1 retenu(s), %2 rejeté(s)").arg(pair.shownInliers).arg(pair.shownOutliers));
    m_errorValueLabel->setText(FrontendUtils::formatMeasure(pair.optimizationErrorPx, 2, QStringLiteral(" px")));
    const bool corrected = !pair.intervention.isEmpty() && pair.intervention != tr("aucune");
    m_correctionTitleLabel->setVisible(corrected);
    m_correctionValueLabel->setVisible(corrected);
    m_correctionValueLabel->setText(pair.intervention);
    m_validateButton->setEnabled(pair.status == FrontendUtils::PairStatus::Dubious);
}

QString AlignmentTab::shortPairLabel(const FrontendUtils::PairAlignmentInfo &pair) const
{
    return m_shortTileNames.value(pair.tileA, pair.tileA) + QStringLiteral(" → ") + m_shortTileNames.value(pair.tileB, pair.tileB);
}

void AlignmentTab::startInteractiveCorrection()
{
    if (m_currentPairIndex < 0 || m_currentPairIndex >= m_pairs.size())
        return;
    const auto &pair = m_pairs.at(m_currentPairIndex);
    if (pair.imagePathA.isEmpty() || pair.imagePathB.isEmpty()
        || pair.tileSizeA.isEmpty() || pair.tileSizeB.isEmpty()) {
        showManualAdjustmentDialog();
        return;
    }
    if (!m_canvas)
        return;
    m_actionsBar->setVisible(false);
    m_pickingBar->setVisible(true);
    const bool alreadyManual = pair.status == FrontendUtils::PairStatus::Validated
                               && pair.statusReason.contains(QStringLiteral("main"));
    if (alreadyManual)
        m_canvas->startPointPicking(pair.pointsInA, pair.pointsInB);
    else
        m_canvas->startPointPicking();
    refreshPickingUi();
    m_mainWindow->updateStatus(tr("Correction manuelle : cliquez un point dans A puis dans B (%1).").arg(pair.pairLabel()));
}

void AlignmentTab::refreshPickingUi()
{
    if (!m_canvas)
        return;
    const int count = static_cast<int>(m_canvas->pickedPointsInA().size());
    const bool pending = m_canvas->hasPendingPointInA();
    m_pickingHintLabel->setText(pending
        ? tr("%1 paire(s) — cliquez le même détail dans B (clic droit : retirer).")
              .arg(count)
        : tr("%1 paire(s) — cliquez un détail dans A, puis le même dans B.")
              .arg(count));
    m_applyPointsButton->setEnabled(count > 0);
    m_undoPointButton->setEnabled(pending || count > 0);
}

void AlignmentTab::cancelPointPickingUi()
{
    if (m_canvas)
        m_canvas->stopPointPicking();
    if (m_pickingBar)
        m_pickingBar->setVisible(false);
    if (m_actionsBar)
        m_actionsBar->setVisible(true);
}

// Ouvre une fenêtre permettant à l'opérateur de corriger manuellement le décalage (dx, dy) de la paire sélectionnée.
void AlignmentTab::showManualAdjustmentDialog()
{
    if (m_currentPairIndex < 0 || m_currentPairIndex >= m_pairs.size())
        return;
    const auto &pair = m_pairs.at(m_currentPairIndex);
    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(tr("Corriger la paire %1").arg(pair.pairLabel()));
    QFormLayout form(&dialog);
    form.addRow(FrontendUtils::mutedLabel(tr("Position du coin haut gauche de B dans A, en pixels."), &dialog));

    auto *dx = new QDoubleSpinBox(&dialog);
    auto *dy = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *spin : {dx, dy}) {
        spin->setRange(-MAXIMUM_OFFSET_PX, MAXIMUM_OFFSET_PX);
        spin->setDecimals(1);
    }
    dx->setValue(pair.dx);
    dy->setValue(pair.dy);
    form.addRow(tr("Décalage horizontal (px)"), dx);
    form.addRow(tr("Décalage vertical (px)"), dy);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
        emit manualOffsetApplied(m_currentPairIndex, dx->value(), dy->value());
}

