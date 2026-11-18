// src/frontend/assembly_tab.cpp
#include "assembly_tab.h"
#include "frontendutils.h"
#include "mainwindow.h"
#include "projectgrid.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {

// Sélectionne l'entrée dont la donnée vaut `value` (nom de step ou de méthode).
void selectData(QComboBox *combo, const std::string &value)
{
    const int index = combo->findData(QString::fromStdString(value));
    if (index >= 0)
        combo->setCurrentIndex(index);
}

std::string selectedData(const QComboBox *combo)
{
    return combo->currentData().toString().toStdString();
}

}

AssemblyTab::AssemblyTab(MainWindow *mainWindow, ProjectGrid *projectGrid, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_projectGrid(projectGrid)
{
    auto *chainLayout = new QVBoxLayout(this);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));

    setupStepList(chainLayout);
    setupActionControls(chainLayout);
    setupMethodControls(chainLayout);
    updateChoices(m_projectGrid->state().pipelineChoices);
}

void AssemblyTab::setupStepList(QVBoxLayout *layout)
{
    m_stepList = new QListWidget(this);
    layout->addWidget(m_stepList);

    // Un double-clic sur une étape déclenche immédiatement la relance à partir de celle-ci.
    connect(m_stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });
}

void AssemblyTab::setupActionControls(QVBoxLayout *layout)
{
    auto *relaunch = FrontendUtils::makeButton(tr("Relancer à partir d'ici"), this);
    layout->addWidget(relaunch);

    // Relance le pipeline à partir de l'étape sélectionnée en exploitant les checkpoints existants.
    connect(relaunch, &QPushButton::clicked, this, [this]() {
        const int row = std::max(0, m_stepList->currentRow());
        m_mainWindow->requestRun(row + 1);
    });
}

// Une méthode par étape qui en propose plusieurs. Les données des entrées sont les noms du registre des steps (ou du
// paramètre method d'intensity-correction) : c'est ce que le script envoie au backend.
void AssemblyTab::setupMethodControls(QVBoxLayout *layout)
{
    auto *methods = new QGroupBox(tr("Méthodes"), this);
    auto *form = new QFormLayout(methods);

    m_overlapCheck = new QCheckBox(tr("Mesurer le recouvrement réel"), methods);
    m_overlapCheck->setToolTip(tr("Ajoute l'étape « Recouvrement réel » : la translation mesurée sert de point de départ "
                                  "à l'appariement, au lieu du recouvrement nominal."));
    form->addRow(m_overlapCheck);

    m_matchingCombo = new QComboBox(methods);
    m_matchingCombo->addItem(tr("Corrélation de phase"), QStringLiteral("phase-correlation"));
    m_matchingCombo->addItem(QStringLiteral("SIFT"), QStringLiteral("sift"));
    m_matchingCombo->addItem(QStringLiteral("LightGlue"), QStringLiteral("lightglue"));
    form->addRow(tr("Appariement"), m_matchingCombo);

    m_voteCheck = new QCheckBox(tr("Rejet des aberrants (Vote 2D)"), methods);
    form->addRow(m_voteCheck);

    m_intensityCombo = new QComboBox(methods);
    m_intensityCombo->addItem(tr("Gains + champ d'éclairage"), QStringLiteral("gain-and-field"));
    m_intensityCombo->addItem(tr("Gains seuls"), QStringLiteral("gain"));
    form->addRow(tr("Égalisation"), m_intensityCombo);

    // Le choix d'algorithme de couture existait déjà ici ; seule la coupe de graphe produit les territoires que lit la fusion.
    m_seamCombo = new QComboBox(methods);
    m_seamCombo->addItem(tr("Coupe de graphe"), QStringLiteral("graphcut"));
    m_seamCombo->addItem(tr("Programmation dynamique"), QStringLiteral("dynamic-programming"));
    m_seamCombo->addItem(tr("Dijkstra"), QStringLiteral("dijkstra"));
    if (auto *model = qobject_cast<QStandardItemModel *>(m_seamCombo->model())) {
        for (int row = 1; row < model->rowCount(); ++row) {
            model->item(row)->setEnabled(false);
            model->item(row)->setToolTip(tr("Pas encore relié à la fusion : cette méthode produit des coutures, "
                                            "pas les territoires qu'utilise l'assemblage."));
        }
    }
    form->addRow(tr("Couture"), m_seamCombo);
    layout->addWidget(methods);

    connect(m_overlapCheck, &QCheckBox::toggled, this, &AssemblyTab::applyChoices);
    connect(m_voteCheck, &QCheckBox::toggled, this, &AssemblyTab::applyChoices);
    for (QComboBox *combo : {m_matchingCombo, m_intensityCombo, m_seamCombo})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &AssemblyTab::applyChoices);
}

void AssemblyTab::applyChoices()
{
    FrontendNs::PipelineChoices choices;
    choices.estimateOverlap = m_overlapCheck->isChecked();
    choices.matchingStep = selectedData(m_matchingCombo);
    // Le Vote 2D n'est pas proposé avec la corrélation de phase (case grisée par updateChoices).
    choices.rejectOutliers = m_voteCheck->isChecked() && FrontendNs::canRejectOutliers(choices);
    choices.intensityMethod = selectedData(m_intensityCombo);
    choices.seamStep = selectedData(m_seamCombo);
    if (choices == m_projectGrid->state().pipelineChoices)
        return;
    m_projectGrid->setPipelineChoices(choices);   // le projet prévient l'interface, qui rappelle updateChoices
    m_mainWindow->updateStatus(tr("Méthodes modifiées — les étapes suivantes sont à refaire."));
}

void AssemblyTab::updateChoices(const FrontendNs::PipelineChoices &choices)
{
    const QSignalBlocker overlapBlocker(m_overlapCheck);
    const QSignalBlocker matchingBlocker(m_matchingCombo);
    const QSignalBlocker voteBlocker(m_voteCheck);
    const QSignalBlocker intensityBlocker(m_intensityCombo);
    const QSignalBlocker seamBlocker(m_seamCombo);
    m_overlapCheck->setChecked(choices.estimateOverlap);
    selectData(m_matchingCombo, choices.matchingStep);
    m_voteCheck->setEnabled(FrontendNs::canRejectOutliers(choices));
    m_voteCheck->setChecked(choices.rejectOutliers && FrontendNs::canRejectOutliers(choices));
    m_voteCheck->setToolTip(FrontendNs::canRejectOutliers(choices)
        ? tr("Ajoute l'étape « Rejet des aberrants » après l'appariement.")
        : tr("Non disponible avec la corrélation de phase, qui donne peu de points par paire."));
    selectData(m_intensityCombo, choices.intensityMethod);
    selectData(m_seamCombo, choices.seamStep);
    refreshStepList(choices);
}

void AssemblyTab::refreshStepList(const FrontendNs::PipelineChoices &choices)
{
    const auto steps = FrontendUtils::pipelineSteps(choices);
    // Mêmes étapes : la liste est gardée telle quelle, avec l'étape sélectionnée pour « Relancer à partir d'ici ».
    bool isUnchanged = m_stepList->count() == static_cast<int>(steps.size());
    for (int row = 0; isUnchanged && row < m_stepList->count(); ++row)
        isUnchanged = m_stepList->item(row)->text() == QStringLiteral("•  ") + steps[static_cast<std::size_t>(row)].displayName;
    if (isUnchanged)
        return;
    m_stepList->clear();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("•  ") + steps[i].displayName, m_stepList);
        item->setData(Qt::UserRole, static_cast<int>(i + 1));
    }
    setActiveStep(m_activeStep);
}

void AssemblyTab::setActiveStep(int stepNumber)
{
    m_activeStep = stepNumber;
    if (!m_stepList) return;

    // Si aucune étape n'est active (ex. calcul terminé ou inactif), désélectionne la liste.
    if (stepNumber <= 0 || stepNumber > m_stepList->count()) {
        m_stepList->clearSelection();
        m_stepList->setCurrentRow(-1);
        return;
    }

    // Sélectionne et fait défiler automatiquement jusqu'à l'étape active.
    m_stepList->setCurrentRow(stepNumber - 1);
    m_stepList->scrollToItem(m_stepList->item(stepNumber - 1));
}