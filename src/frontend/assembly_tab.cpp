// src/frontend/assembly_tab.cpp
#include "assembly_tab.h"
#include "frontendutils.h"
#include "mainwindow.h"

#include <algorithm>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

AssemblyTab::AssemblyTab(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    auto *chainLayout = new QVBoxLayout(this);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));

    setupStepList(chainLayout);
    setupActionControls(chainLayout);
}

void AssemblyTab::setupStepList(QVBoxLayout *layout)
{
    m_stepList = new QListWidget(this);
    const auto& steps = FrontendUtils::pipelineSteps();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("•  ") + steps[i].displayName, m_stepList);
        item->setData(Qt::UserRole, static_cast<int>(i + 1));
        item->setToolTip(tr("Double-cliquez pour relancer le pipeline à partir de « %1 »")
                             .arg(steps[i].displayName));
    }
    layout->addWidget(m_stepList);

    // Un double-clic sur une étape déclenche immédiatement la relance à partir de celle-ci.
    connect(m_stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });
}

void AssemblyTab::setupActionControls(QVBoxLayout *layout)
{
    auto *relaunch = FrontendUtils::makeButton(tr("Sélectionnez une étape à relancer"), this);
    relaunch->setEnabled(false);
    layout->addWidget(relaunch);

    connect(m_stepList, &QListWidget::currentRowChanged, this, [this, relaunch](int row) {
        if (row < 0 || !m_stepList->item(row)) {
            relaunch->setEnabled(false);
            relaunch->setText(tr("Sélectionnez une étape à relancer"));
            return;
        }
        const QString stepName = m_stepList->item(row)->text().remove(QStringLiteral("•  "));
        relaunch->setEnabled(true);
        relaunch->setText(tr("Relancer depuis : %1").arg(stepName));
    });

    // Relance le pipeline à partir de l'étape sélectionnée en exploitant les checkpoints existants.
    connect(relaunch, &QPushButton::clicked, this, [this]() {
        const int row = std::max(0, m_stepList->currentRow());
        m_mainWindow->requestRun(row + 1);
    });

    auto *selectAlgo = new QComboBox(this);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    layout->addWidget(selectAlgo);

    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        m_mainWindow->updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
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