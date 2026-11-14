// src/frontend/top_toolbars.cpp
#include "top_toolbars.h"
#include "frontendutils.h"
#include "mainwindow.h"
#include "projectgrid.h"
#include "simulationrunner.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TopToolbars::TopToolbars(MainWindow *mainWindow, ProjectGrid *projectGrid, SimulationRunner *simulationRunner, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_projectGrid(projectGrid), m_simulationRunner(simulationRunner)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    createToolbar();
    createModesBar();
    createOptionsBar();

    // Synchronise dynamiquement le texte du bouton principal avec le cycle de vie du calcul.
    if (m_simulationRunner) {
        connect(m_simulationRunner, &SimulationRunner::runStarted, this, &TopToolbars::updateRunButtonState);
        connect(m_simulationRunner, &SimulationRunner::runFinished, this, &TopToolbars::updateRunButtonState);
        connect(m_simulationRunner, &SimulationRunner::runCancelled, this, &TopToolbars::updateRunButtonState);
        connect(m_simulationRunner, &SimulationRunner::pausedStateChanged, this, &TopToolbars::updateRunButtonState);
    }
}

void TopToolbars::createToolbar()
{
    QWidget *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);

    auto *title = new QLabel(tr("KIK-IRPA Assemblage"));
    title->setObjectName(QStringLiteral("workspaceTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();

    auto *save = FrontendUtils::makeButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, m_mainWindow, &MainWindow::saveProject);

    static_cast<QVBoxLayout *>(layout())->addWidget(toolbar);
}

void TopToolbars::createModeButtons(QWidget *stages, QHBoxLayout *stageLayout)
{
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);

    for (int i = 0; i < FrontendUtils::MODE_NAMES.size(); ++i) {
        const QString label = (i == 3)
            ? QStringLiteral("!   %1").arg(FrontendUtils::MODE_NAMES.at(i))
            : QStringLiteral("%1   %2").arg(i + 1).arg(FrontendUtils::MODE_NAMES.at(i));
        auto *button = FrontendUtils::makeButton(label, stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { m_mainWindow->setMode(i); });
    }
}

void TopToolbars::createActionButtons(QWidget *stages, QHBoxLayout *stageLayout)
{
    // Bouton polymorphe d'exécution du calcul (Lancer / Pause / Reprendre).
    m_runButton = FrontendUtils::makeButton(tr("▶ Tout lancer"), stages, true);
    m_runButton->setObjectName(QStringLiteral("runPipelineButton"));
    stageLayout->addWidget(m_runButton);
    connect(m_runButton, &QPushButton::clicked, this, &TopToolbars::handleRunButtonClicked);

    auto *compare = FrontendUtils::makeButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked, m_simulationRunner, &SimulationRunner::showComparison);
}

void TopToolbars::createModesBar()
{
    QWidget *stages = new QWidget(this);
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);

    createModeButtons(stages, stageLayout);

    auto *prevPairBtn = FrontendUtils::makeButton(tr("‹ Préc."), stages);
    prevPairBtn->setObjectName(QStringLiteral("prevPairButton"));
    stageLayout->addWidget(prevPairBtn);
    connect(prevPairBtn, &QPushButton::clicked, this, &TopToolbars::previousPairRequested);

    auto *nextDubiousBtn = FrontendUtils::makeButton(tr("Douteuse suivante N ›"), stages);
    nextDubiousBtn->setObjectName(QStringLiteral("nextDubiousButton"));
    stageLayout->addWidget(nextDubiousBtn);
    connect(nextDubiousBtn, &QPushButton::clicked, this, &TopToolbars::nextDubiousPairRequested);

    stageLayout->addStretch();
    createActionButtons(stages, stageLayout);

    static_cast<QVBoxLayout *>(layout())->addWidget(stages);
}

void TopToolbars::handleRunButtonClicked()
{
    if (!m_simulationRunner) return;

    // Logique à 3 états :
    // 1. En cours d'exécution -> demande une mise en pause propre après l'étape active.
    // 2. En pause -> reprend l'exécution là où elle s'était arrêtée.
    // 3. Inactif -> démarre une exécution complète depuis l'étape 1.

    if (m_simulationRunner->isRunning()) {
        m_simulationRunner->pauseRun();
    } else if (m_simulationRunner->isPaused()) {
        m_simulationRunner->resumeRun();
    } else {
        m_mainWindow->requestRun(1);
    }
}

void TopToolbars::updateRunButtonState()
{
    if (!m_runButton || !m_simulationRunner) return;

    // Met à jour l'icône et l'intitulé du bouton selon l'état réel du runner backend.
    if (m_simulationRunner->isRunning()) {
        m_runButton->setText(tr("⏸ Pause"));
    } else if (m_simulationRunner->isPaused()) {
        m_runButton->setText(tr("▶ Reprendre"));
    } else {
        m_runButton->setText(tr("▶ Tout lancer"));
    }
}

void TopToolbars::createOptionsBar()
{
    QWidget *modeOptions = new QWidget(this);
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
    connect(m_arrangementCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int value) {
        m_projectGrid->setCaptureOrder(value);
        m_mainWindow->updateStatus(tr("Ordre de prise de vue : %1").arg(m_arrangementCombo->currentText()));
    });

    static_cast<QVBoxLayout *>(layout())->addWidget(modeOptions);
}

void TopToolbars::updateTitle(const QString &projectName, const QString &modality)
{
    if (auto *title = findChild<QLabel *>(QStringLiteral("workspaceTitle"))) {
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage").arg(projectName, modality));
    }
}

void TopToolbars::updateDubiousState(int dubiousCount)
{
    if (auto *alignBtn = findChild<QPushButton *>(QStringLiteral("mode3"))) {
        if (dubiousCount > 0) {
            alignBtn->setText(QStringLiteral("!   %1 (%2)").arg(FrontendUtils::MODE_NAMES.at(3)).arg(dubiousCount));
        } else {
            alignBtn->setText(QStringLiteral("✓   %1").arg(FrontendUtils::MODE_NAMES.at(3)));
        }
    }
    if (auto *nextDubiousBtn = findChild<QPushButton *>(QStringLiteral("nextDubiousButton"))) {
        nextDubiousBtn->setEnabled(dubiousCount > 0);
    }
}