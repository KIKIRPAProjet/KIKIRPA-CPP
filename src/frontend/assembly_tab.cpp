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

namespace {

    // Liste ordonnée des 5 étapes réelles exécutées par le pipeline backend.
    // Correspond aux identifiants backend : import, phase-correlation, absolute-pos,
    // intensity-correction et graphcut définis dans MainWindow.
    const QStringList BACKEND_DISPLAY_STEPS = {
        QStringLiteral("1  Importation des tuiles"),
        QStringLiteral("2  Correspondance (Appariement)"),
        QStringLiteral("3  Positionnement global"),
        QStringLiteral("4  Égalisation de luminosité"),
        QStringLiteral("5  Ligne de couture (GraphCut)")
    };

} // namespace

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
    for (int i = 0; i < BACKEND_DISPLAY_STEPS.size(); ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("•  ") + BACKEND_DISPLAY_STEPS.at(i), m_stepList);
        // Associe le numéro d'étape 1-indexé (1 à 5) à l'élément pour faciliter les requêtes de relance.
        item->setData(Qt::UserRole, i + 1);
    }
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