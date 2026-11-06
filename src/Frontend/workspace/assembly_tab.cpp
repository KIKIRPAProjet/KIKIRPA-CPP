/**
 * @brief assemblage de la chaine de de traitement
 * Crée l'interface pour le mode assemblage.
 */
#include "Frontend/workspace/assembly_tab.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/common/frontendutils.h"

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

    auto *stepList = new QListWidget(this);
    for (int i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem((i == 2 ? QStringLiteral("⚠  ") : QStringLiteral("●  "))
                                         + steps.at(i), stepList);
        item->setData(Qt::UserRole, i + 1);
    }
    chainLayout->addWidget(stepList);

    connect(stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });

    //bouton de relance 
    auto *relaunch = FrontendUtils::makeButton(tr("Relancer à partir d'ici"), this);
    chainLayout->addWidget(relaunch);

    connect(relaunch, &QPushButton::clicked, this, [this, stepList]() {
        const int row = qMax(0, stepList->currentRow());
        m_mainWindow->requestRun(row + 1);
    });

    auto *selectAlgo = new QComboBox(this);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    chainLayout->addWidget(selectAlgo);

    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        m_mainWindow->updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
}