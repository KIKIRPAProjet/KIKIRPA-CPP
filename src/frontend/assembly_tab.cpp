/**
 * @brief assemblage de la chaine de de traitement
 * Crée l'interface pour le mode assemblage.
 */
#include "assembly_tab.h"
#include "mainwindow.h"
#include "frontendutils.h"

#include <algorithm>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>

AssemblyTab::AssemblyTab(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
     auto *chainLayout = new QVBoxLayout(this);
    chainLayout->setContentsMargins(6, 8, 6, 8);

    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));

    const QStringList steps = {tr("1  Import"), tr("2  Masques"), tr("3  Correspondance"),
        tr("4  Optimisation"), tr("5  Calage local"), tr("6  Égalisation"),
        tr("7  Ligne de couture"), tr("8  Fusion et export")};

    m_stepList = new QListWidget(this);
    for (int i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem((i == 2 ? QStringLiteral("⚠  ") : QStringLiteral("●  "))
                                         + steps.at(i), m_stepList);
        item->setData(Qt::UserRole, i + 1);
    }
    
    chainLayout->addWidget(m_stepList);

    // Résumé de l'état sauvegardé, rempli à l'ouverture d'un projet (showSavedState)
    m_savedStateLabel = new QLabel(this);
    m_savedStateLabel->setWordWrap(true);
    chainLayout->addWidget(m_savedStateLabel);

    connect(m_stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });

    // Bouton de relance 
    auto *relaunch = FrontendUtils::makeButton(tr("Relancer à partir d'ici"), this);
    chainLayout->addWidget(relaunch);

    connect(relaunch, &QPushButton::clicked, this, [this]() {
        const int row = std::max(0, m_stepList->currentRow());
        m_mainWindow->requestRun(row + 1);
    });

    auto *selectAlgo = new QComboBox(this);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    chainLayout->addWidget(selectAlgo);

    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        m_mainWindow->updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
}

void AssemblyTab::setActiveStep(int stepNumber)
{
    m_activeStep = stepNumber;
    if (!m_stepList)
        return;

    if (stepNumber <= 0 || stepNumber > m_stepList->count()) {
        m_stepList->clearSelection();
        m_stepList->setCurrentRow(-1);
        return;
    }

    m_stepList->setCurrentRow(stepNumber - 1);
    m_stepList->scrollToItem(m_stepList->item(stepNumber - 1));
}

void AssemblyTab::showSavedState(const QStringList &stepNames, int savedStepCount)
{
    if (stepNames.isEmpty()) {
        m_savedStateLabel->setText(tr("Aucun calcul sauvegardé pour ce projet."));
        return;
    }

    // Une ligne par step du script, numérotée comme dans le pipeline
    m_stepList->clear();
    for (int i = 0; i < stepNames.size(); ++i) {
        QString marker = QStringLiteral("○  ");
        if (i < savedStepCount)
            marker = QStringLiteral("✓  ");
        else if (i == savedStepCount)
            marker = QStringLiteral("▶  ");
        auto *item = new QListWidgetItem(marker + QString::number(i + 1) + QStringLiteral("  ") + stepNames.at(i),
                                         m_stepList);
        item->setData(Qt::UserRole, i + 1);
    }

    if (savedStepCount >= stepNames.size())
        m_savedStateLabel->setText(tr("Toutes les étapes sont sauvegardées (%1/%1).").arg(stepNames.size()));
    else
        m_savedStateLabel->setText(tr("%1/%2 étape(s) sauvegardée(s). Le calcul reprendra à : %3.")
                                       .arg(savedStepCount).arg(stepNames.size()).arg(stepNames.at(savedStepCount)));
}
