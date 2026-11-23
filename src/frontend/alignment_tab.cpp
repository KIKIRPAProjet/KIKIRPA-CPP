/*
 * .cpp : le POURQUOI
 * - choix de conception : regrouper la sélection, le diagnostic et les actions sur une paire dans l'onglet Paire ;
 * - cas limite : filtrer uniquement les paires douteuses peut réduire la liste, d'où le blocage des signaux lors du remplissage ;
 * - unité ou repère : les décalages dx/dy et erreurs résiduelles sont exprimés en pixels (px) ;
 * - raison non évidente : conserver les étiquettes de métadonnées projet évite de casser les mises à jour globales de RightPanel.
 */
/*
 * Header : le QUOI
 * Affiche le panneau latéral « Paire » pour inspecter, filtrer et valider les paires douteuses.
 * - ce que ça fait : présente le statut, le score de confiance, le contrôle NCC, l'erreur et les actions d'intervention ;
 * - ce que ça renvoie : émet des signaux lors de la sélection, de la validation ou de la correction manuelle d'une paire ;
 * - quand ça lève une exception : si l'allocation d'un widget Qt échoue.
 */
#include "alignment_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QSignalBlocker>

AlignmentTab::AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_canvas(canvas)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Recherche et sélection rapide des paires douteuses
    auto *selectorTitle = FrontendUtils::mutedLabel(tr("Sélection de paire"), this);
    mainLayout->addWidget(selectorTitle);

    m_pairCombo = new QComboBox(this);
    m_pairCombo->setObjectName(QStringLiteral("pairSelectorCombo"));
    mainLayout->addWidget(m_pairCombo);

    auto *filterRow = new QHBoxLayout;
    m_onlyDubiousCheck = new QCheckBox(tr("Douteuses uniquement"), this);
    m_nextDubiousButton = FrontendUtils::makeButton(tr("Douteuse suiv. N"), this);
    filterRow->addWidget(m_onlyDubiousCheck);
    filterRow->addStretch();
    filterRow->addWidget(m_nextDubiousButton);
    mainLayout->addLayout(filterRow);

    auto *separatorTop = new QFrame(this);
    separatorTop->setFrameShape(QFrame::HLine);
    separatorTop->setStyleSheet(QStringLiteral("color:#424242;"));
    mainLayout->addWidget(separatorTop);

    // En-tête de la paire courante + statut (douteuse / fiable)
    auto *headerRow = new QHBoxLayout;
    m_pairTitleLabel = new QLabel(tr("Paire r00_c00 → r00_c01"), this);
    m_pairTitleLabel->setStyleSheet(QStringLiteral("font-weight:600; color:#ececec;"));
    m_statusBadgeLabel = new QLabel(tr("douteuse"), this);
    m_statusBadgeLabel->setAlignment(Qt::AlignCenter);
    headerRow->addWidget(m_pairTitleLabel, 1);
    headerRow->addWidget(m_statusBadgeLabel);
    mainLayout->addLayout(headerRow);

    // Tableau de diagnostic de la paire 
    auto *metricsLayout = new QFormLayout;
    metricsLayout->setContentsMargins(0, 2, 0, 2);
    metricsLayout->setHorizontalSpacing(10);
    metricsLayout->setVerticalSpacing(5);

    m_reasonValueLabel = new QLabel(this);
    m_reasonValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_reasonValueLabel->setWordWrap(true);

    m_confidenceValueLabel = new QLabel(this);
    m_confidenceValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_offsetValueLabel = new QLabel(this);
    m_offsetValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_pointsShownValueLabel = new QLabel(this);
    m_pointsShownValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_pointsShownValueLabel->setTextFormat(Qt::RichText);

    m_errorValueLabel = new QLabel(this);
    m_errorValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_interventionValueLabel = new QLabel(this);
    m_interventionValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Raison"), this), m_reasonValueLabel);
    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Confiance / NCC"), this), m_confidenceValueLabel);
    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Décalage mesuré"), this), m_offsetValueLabel);
    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Points montrés"), this), m_pointsShownValueLabel);
    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Erreur après optimisation"), this), m_errorValueLabel);
    metricsLayout->addRow(FrontendUtils::mutedLabel(tr("Interventions"), this), m_interventionValueLabel);
    mainLayout->addLayout(metricsLayout);

    //Grille 2x2 des boutons d'intervention + bouton primaire de recalcul
    auto *actionsGrid = new QGridLayout;
    actionsGrid->setHorizontalSpacing(6);
    actionsGrid->setVerticalSpacing(6);

    auto *addPointButton = FrontendUtils::makeButton(tr("Ajouter un point P"), this);
    auto *deletePointButton = FrontendUtils::makeButton(tr("Supprimer Suppr"), this);
    auto *adjustButton = FrontendUtils::makeButton(tr("Saisir un décalage…"), this);
    m_validateButton = FrontendUtils::makeButton(tr("Valider la paire"), this);

    actionsGrid->addWidget(addPointButton, 0, 0);
    actionsGrid->addWidget(deletePointButton, 0, 1);
    actionsGrid->addWidget(adjustButton, 1, 0);
    actionsGrid->addWidget(m_validateButton, 1, 1);
    mainLayout->addLayout(actionsGrid);

    auto *recalcButton = FrontendUtils::makeButton(tr("↻ Recalculer les positions F7"), this, true);
    mainLayout->addWidget(recalcButton);

    auto *separatorBottom = new QFrame(this);
    separatorBottom->setFrameShape(QFrame::HLine);
    separatorBottom->setStyleSheet(QStringLiteral("color:#424242;"));
    mainLayout->addWidget(separatorBottom);

    // Résumé du projet 
    m_projectNameLabel = FrontendUtils::mutedLabel(QString(), this);
    m_projectNameLabel->setWordWrap(true);
    mainLayout->addWidget(m_projectNameLabel);

    m_tileCountLabel = FrontendUtils::mutedLabel(QString(), this);
    mainLayout->addWidget(m_tileCountLabel);

    m_projectMetadataLabel = FrontendUtils::mutedLabel(QString(), this);
    m_projectMetadataLabel->setObjectName(QStringLiteral("projectMetadataLabel"));
    mainLayout->addWidget(m_projectMetadataLabel);

    mainLayout->addStretch();

    // Connexions des interactions
    connect(m_pairCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int comboIndex) {
        if (comboIndex < 0)
            return;
        const int pairIdx = m_pairCombo->itemData(comboIndex).toInt();
        m_mainWindow->setMode(3);
        emit pairSelectionRequested(pairIdx);
    });

    connect(m_onlyDubiousCheck, &QCheckBox::toggled, this, [this](bool) {
        rebuildPairSelector();
    });

    connect(m_nextDubiousButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->setMode(3);
        emit nextDubiousRequested();
    });

    connect(addPointButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->setMode(3);
        m_mainWindow->updateStatus(tr("Outil Point de contrôle (P) actif : cliquez dans la tuile A puis dans la tuile B."));
    });

    connect(deletePointButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->updateStatus(tr("Point de correspondance sélectionné supprimé."));
    });

    connect(adjustButton, &QPushButton::clicked,
            this, &AlignmentTab::showManualAdjustmentDialog);

    connect(m_validateButton, &QPushButton::clicked, this, [this]() {
        emit pairValidated(m_currentPairIndex);
    });

    connect(recalcButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->requestRun(4);
    });

    setPairs(FrontendUtils::defaultPairAlignments(), 0);
}

// enregistre la liste des paires et l'index de la paire active 
void AlignmentTab::setPairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs, int currentPairIndex)
{
    m_pairs = pairs;
    if (!m_pairs.isEmpty()) {
        m_currentPairIndex = qBound(0, currentPairIndex, m_pairs.size() - 1);
    } else {
        m_currentPairIndex = 0;
    }
    rebuildPairSelector();
    if (!m_pairs.isEmpty()) {
        setCurrentPair(m_pairs.at(m_currentPairIndex));
    }
}

// Met à jour la liste déroulante des paires (toutes les paires, ou seulement les douteuses si le filtre est coché).
void AlignmentTab::rebuildPairSelector()
{
    const QSignalBlocker blocker(m_pairCombo); //QSignalBlocker permet de ne pas interférer avec la liste déroulante quand elle se remplie
    m_pairCombo->clear();

    const bool onlyDubious = m_onlyDubiousCheck && m_onlyDubiousCheck->isChecked();
    int comboIndexToSelect = -1;

    for (int i = 0; i < m_pairs.size(); ++i) {
        const auto &pair = m_pairs.at(i);
        if (onlyDubious && !pair.isDubious())
            continue;

        const QString prefix = pair.isDubious()
            ? QStringLiteral("⚠ ")
            : QStringLiteral("✓ ");
        const QString statusText = pair.isDubious()
            ? tr("douteuse")
            : (pair.status == FrontendUtils::PairStatus::Validated ? tr("validée") : tr("fiable"));
        const QString itemLabel = QStringLiteral("%1%2 (%3)")
                                      .arg(prefix, pair.pairLabel(), statusText);
        m_pairCombo->addItem(itemLabel, pair.pairIndex);
        if (pair.pairIndex == m_currentPairIndex) {
            comboIndexToSelect = m_pairCombo->count() - 1;
        }
    }

    if (m_pairCombo->count() == 0 && onlyDubious) {
        m_pairCombo->addItem(tr("Aucune paire douteuse restante"), -1);
    } else if (comboIndexToSelect >= 0) {
        m_pairCombo->setCurrentIndex(comboIndexToSelect);
    }
}

//met à jour l'affichage selon le status de la paire 
void AlignmentTab::setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair)
{
    m_currentPairIndex = pair.pairIndex;

    if (m_currentPairIndex >= 0 && m_currentPairIndex < m_pairs.size()) {
        m_pairs[m_currentPairIndex] = pair;
    }
    rebuildPairSelector();

    m_pairTitleLabel->setText(tr("Paire %1").arg(pair.pairLabel()));

    if (pair.status == FrontendUtils::PairStatus::Dubious) {
        m_statusBadgeLabel->setText(tr("douteuse"));
        m_statusBadgeLabel->setStyleSheet(QStringLiteral(
            "background:#d9822b; color:#1c1c1c; font-weight:600; border-radius:8px; padding:2px 8px;"));
        m_errorValueLabel->setStyleSheet(QStringLiteral("color:#e69138; font-weight:600;"));
        m_validateButton->setEnabled(true);
    } else if (pair.status == FrontendUtils::PairStatus::Failed) {
        m_statusBadgeLabel->setText(tr("échec"));
        m_statusBadgeLabel->setStyleSheet(QStringLiteral(
            "background:#d94a38; color:white; font-weight:600; border-radius:8px; padding:2px 8px;"));
        m_errorValueLabel->setStyleSheet(QStringLiteral("color:#ef5149; font-weight:600;"));
        m_validateButton->setEnabled(true);
    } else if (pair.status == FrontendUtils::PairStatus::Validated) {
        m_statusBadgeLabel->setText(tr("validée"));
        m_statusBadgeLabel->setStyleSheet(QStringLiteral(
            "background:#389e5c; color:white; font-weight:600; border-radius:8px; padding:2px 8px;"));
        m_errorValueLabel->setStyleSheet(QStringLiteral("color:#d5d5d5;"));
        m_validateButton->setEnabled(false);
    } else {
        m_statusBadgeLabel->setText(tr("fiable"));
        m_statusBadgeLabel->setStyleSheet(QStringLiteral(
            "background:#389e5c; color:white; font-weight:600; border-radius:8px; padding:2px 8px;"));
        m_errorValueLabel->setStyleSheet(QStringLiteral("color:#d5d5d5;"));
        m_validateButton->setEnabled(false);
    }

    m_reasonValueLabel->setText(pair.statusReason);
    m_confidenceValueLabel->setText(
        tr("%1 · NCC %2")
            .arg(QString::number(pair.meanConfidence, 'f', 2).replace('.', ','),
                 QString::number(pair.nccScore, 'f', 2).replace('.', ',')));

    m_offsetValueLabel->setText(
        tr("%1 ; %2 px")
            .arg(QString::number(pair.dx, 'f', 1).replace('.', ','),
                 QString::number(pair.dy, 'f', 1).replace('.', ',')));

    m_pointsShownValueLabel->setText(
        QStringLiteral("<span style='color:#4ec978;'>● %1 retenus</span> · "
                       "<span style='color:#e05a52;'>● %2 rejetés</span>")
            .arg(pair.shownInliers)
            .arg(pair.shownOutliers));

    m_errorValueLabel->setText(
        tr("%1 px").arg(QString::number(pair.optimizationErrorPx, 'f', 2).replace('.', ',')));
    m_interventionValueLabel->setText(pair.intervention);
}

//correction d'une paire douteuse à la main 
// Ouvre une fenêtre permettant à l'opérateur de corriger manuellement le décalage (dx, dy) de la paire sélectionnée.
void AlignmentTab::showManualAdjustmentDialog()
{
    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(tr("Correction manuelle"));
    QFormLayout form(&dialog);

    double initialDx = 4065.5;
    double initialDy = -20.7;
    if (m_currentPairIndex >= 0 && m_currentPairIndex < m_pairs.size()) {
        initialDx = m_pairs.at(m_currentPairIndex).dx;
        initialDy = m_pairs.at(m_currentPairIndex).dy;
    }

    auto *dx = new QDoubleSpinBox(&dialog);
    auto *dy = new QDoubleSpinBox(&dialog);
    dx->setRange(-100000, 100000);
    dx->setDecimals(1);
    dx->setValue(initialDx);

    dy->setRange(-100000, 100000);
    dy->setDecimals(1);
    dy->setValue(initialDy);

    form.addRow(tr("Décalage horizontal (px)"), dx);
    form.addRow(tr("Décalage vertical (px)"), dy);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        emit manualOffsetApplied(m_currentPairIndex, dx->value(), dy->value());
        m_mainWindow->updateStatus(tr("Correction enregistrée : dx %1 px, dy %2 px.")
                                       .arg(dx->value())
                                       .arg(dy->value()));
    }
}

void AlignmentTab::updateInfo(const QString &projectName, const ProjectGrid::State &gridState)
{
    m_projectNameLabel->setText(tr("Projet : %1").arg(projectName));
    m_tileCountLabel->setText(
        tr("%1 tuile(s) chargée(s)").arg(gridState.tilePaths.size()));

    const QString scaleText = gridState.scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(gridState.scaleMmPerPixel, 0, 'g', 8)
        : tr("à renseigner");

    m_projectMetadataLabel->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                                        .arg(gridState.modality, scaleText)
                                        .arg(gridState.horizontalOverlap)
                                        .arg(gridState.verticalOverlap));
}

