#include "top_toolbars.h"
#include "mainwindow.h"
#include "projectgrid.h"
#include "simulationrunner.h"
#include "frontendutils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>

TopToolbars::TopToolbars(MainWindow *mainWindow, ProjectGrid *projectGrid, SimulationRunner *simulationRunner, QWidget *parent)
    :QWidget(parent), m_mainWindow(mainWindow), m_projectGrid(projectGrid), m_simulationRunner(simulationRunner)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    createToolbar();
    createModesBar();
    createOptionsBar();
}

void TopToolbars::createToolbar()
{
    // creation du widget 
    QWidget *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);
    
    // creation du titre et du bouton enregistrer
    auto *title = new QLabel(tr("KIK-IRPA Assemblage"));
    title->setObjectName(QStringLiteral("workspaceTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();
    
    auto *save = FrontendUtils::makeButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, m_mainWindow, &MainWindow::saveProject);
    
    static_cast<QVBoxLayout *>(layout())->addWidget(toolbar);
}

void TopToolbars::createModesBar()
{
    QWidget *stages = new QWidget(this);
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);
    
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);
    
    for (int i = 0; i < FrontendUtils::MODE_NAMES.size(); ++i) {
        auto *button = FrontendUtils::makeButton(QStringLiteral("%1   %2").arg(i + 1).arg(FrontendUtils::MODE_NAMES.at(i)), stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { m_mainWindow->setMode(i); });
    }

    m_runButton = FrontendUtils::makeButton(tr("▶ Tout lancer"), stages, true);
    m_runButton->setObjectName(QStringLiteral("runAllButton"));
    stageLayout->addWidget(m_runButton);
    connect(m_runButton, &QPushButton::clicked, this, [this]() { m_mainWindow->requestRun(1); });

    // Bouton dédié à la reprise après annulation/pause (activé uniquement lorsqu'un état repris existe).
    m_resumeButton = FrontendUtils::makeButton(tr("▶ Reprendre"), stages, true);
    m_resumeButton->setObjectName(QStringLiteral("resumeRunButton"));
    m_resumeButton->setEnabled(false);
    m_resumeButton->setToolTip(tr("Reprendre l'assemblage interrompu à partir de la dernière étape sauvegardée."));
    stageLayout->addWidget(m_resumeButton);
    connect(m_resumeButton, &QPushButton::clicked, m_mainWindow, &MainWindow::requestResume);
    
    auto *compare = FrontendUtils::makeButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked,
            m_simulationRunner, &SimulationRunner::showComparison);
    
    static_cast<QVBoxLayout *>(layout())->addWidget(stages);
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
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage")
                           .arg(projectName, modality));
    }
}

void TopToolbars::setResumeAvailable(bool available)
{
    m_resumeAvailable = available;
    if (m_resumeButton)
        m_resumeButton->setEnabled(m_resumeAvailable && !m_runInProgress);
}

void TopToolbars::setRunInProgress(bool running)
{
    m_runInProgress = running;
    if (m_runButton)
        m_runButton->setEnabled(!m_runInProgress);
    if (m_resumeButton)
        m_resumeButton->setEnabled(m_resumeAvailable && !m_runInProgress);
}