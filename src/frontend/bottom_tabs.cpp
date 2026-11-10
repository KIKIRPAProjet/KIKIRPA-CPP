#include "bottom_tabs.h"
#include "mainwindow.h"

#include <QDateTime>
#include <QTableWidget>
#include <QListWidget>
#include <QHeaderView>
#include <QStringList>

BottomTabs::BottomTabs(MainWindow *mainWindow, QWidget *parent)
    : QTabWidget(parent), m_mainWindow(mainWindow)
{
    setMinimumHeight(80);

    m_pairsTable = new QTableWidget(4, 8, this);
    m_pairsTable->setHorizontalHeaderLabels({tr("Paire"), tr("Sens"), tr("Statut"), tr("Confiance"),
        tr("Contrôle NCC"), tr("dx (px)"), tr("dy (px)"), tr("Erreur")});
    
    const QStringList pairRows[] = {
        {QStringLiteral("r00_c00 → r00_c01"), tr("horizontal"), tr("douteuse"), QStringLiteral("0,91"), QStringLiteral("0,37"), QStringLiteral("4 065,5"), QStringLiteral("-20,7"), QStringLiteral("3,95 px")},
        {QStringLiteral("r00_c00 → r01_c00"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,76"), QStringLiteral("23,5"), QStringLiteral("4 086,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r00_c01 → r01_c01"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,92"), QStringLiteral("23,8"), QStringLiteral("4 133,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r01_c00 → r01_c01"), tr("horizontal"), tr("fiable"), QStringLiteral("0,98"), QStringLiteral("0,65"), QStringLiteral("4 064,7"), QStringLiteral("29,9"), QStringLiteral("0,02 px")}
    };

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col)
            m_pairsTable->setItem(row, col, new QTableWidgetItem(pairRows[row].at(col)));
    }

    m_pairsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pairsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pairsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    addTab(m_pairsTable, tr("Paires (4)"));
    
    m_journalList = new QListWidget;
    m_journalList->addItems({tr("14:12  Assemblage terminé — une paire à vérifier"),
                       tr("14:10  4 tuiles importées"),
                       tr("14:02  Projet ouvert")});
    addTab(m_journalList, tr("Journal"));

    auto *positions = new QTableWidget(4, 3);
    positions->setHorizontalHeaderLabels({tr("Tuile"), tr("X"), tr("Y")});
    for (int i = 0; i < 4; ++i) {
        positions->setItem(i, 0, new QTableWidgetItem(QStringLiteral("r%1_c%2").arg(i / 2).arg(i % 2)));
        positions->setItem(i, 1, new QTableWidgetItem(QString::number((i % 2) * 4065)));
        positions->setItem(i, 2, new QTableWidgetItem(QString::number((i / 2) * 4080)));
    }
    addTab(positions, tr("Positions"));

    connect(m_pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { m_mainWindow->setMode(3); });
    connect(m_pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        m_mainWindow->updateStatus(tr("Paire sélectionnée : %1").arg(m_pairsTable->item(row, 0)->text()));
    });

}

void BottomTabs::appendJournalEntry(const QString &message)
{
    if (!m_journalList)
        return;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_journalList->insertItem(0, QStringLiteral("%1  %2").arg(timestamp, message));
}