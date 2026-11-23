/*
 * .cpp : le POURQUOI
 * - choix de conception : afficher le tableau des paires en bas permet de comparer d'un coup d'œil les scores de confiance et NCC ;
 * - cas limite : bloquer les signaux lors de la sélection par code évite une boucle de rétroaction avec le panneau de droite ;
 * - unité ou repère : les translations dx/dy et erreurs résiduelles sont affichées en pixels (px) ;
 * - raison non évidente : colorer le statut et l'erreur après optimisation attire immédiatement l'attention sur les paires douteuses.
 */
/*
 * Header : le QUOI
 * Gère les onglets inférieurs (Paires, Journal, Positions) et la sélection dans la table des paires.
 * - ce que ça fait : affiche la liste complète des paires avec leur statut et synchronise la sélection avec la vue Paire ;
 * - ce que ça renvoie : émet pairSelected(int) lorsqu'une paire est choisie par l'utilisateur ;
 * - quand ça lève une exception : si la création d'un élément de tableau échoue.
 */
#include "bottom_tabs.h"
#include "mainwindow.h"

#include <QTableWidget>
#include <QListWidget>
#include <QHeaderView>
#include <QStringList>
#include <QSignalBlocker>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QTime>

BottomTabs::BottomTabs(MainWindow *mainWindow, QWidget *parent)
    : QTabWidget(parent), m_mainWindow(mainWindow)
{
    m_journalList = new QListWidget(this);
    addTab(m_journalList, tr("Journal"));

    m_pairsTable = new QTableWidget(0, 10, this);
    m_pairsTable->setHorizontalHeaderLabels({
        tr("Paire"), tr("Sens"), tr("Statut"), tr("Confiance"),
        tr("Contrôle NCC"), tr("dx (px)"), tr("dy (px)"),
        tr("Points (inliers / total)"), tr("Erreur après optimisation"), tr("Correction")
    });

    auto *header = m_pairsTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(9, QHeaderView::Stretch);
    if (auto *confHeader = m_pairsTable->horizontalHeaderItem(3))
        confHeader->setToolTip(tr("Score de confiance moyen des correspondances (0 à 1)"));
    if (auto *nccHeader = m_pairsTable->horizontalHeaderItem(4))
        nccHeader->setToolTip(tr("Corrélation croisée normalisée sur la zone de recouvrement"));
    m_pairsTable->setAlternatingRowColors(true);
    m_pairsTable->setSortingEnabled(true);
    m_pairsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pairsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pairsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pairsTabIndex = addTab(m_pairsTable, tr("Paires"));

    auto *positions = new QTableWidget(0, 3, this);
    positions->setHorizontalHeaderLabels({tr("Tuile"), tr("X"), tr("Y")});
    positions->setEditTriggers(QAbstractItemView::NoEditTriggers);
    addTab(positions, tr("Positions"));

    updatePairs({});
    setCurrentIndex(m_pairsTabIndex);

    connect(m_pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto *firstItem = m_pairsTable->item(row, 0);
        const int pairIndex = firstItem ? firstItem->data(Qt::UserRole).toInt() : row;
        m_mainWindow->setMode(3);
        emit pairSelected(pairIndex);
    });
    connect(m_pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto *firstItem = m_pairsTable->item(row, 0);
        const int pairIndex = firstItem ? firstItem->data(Qt::UserRole).toInt() : row;
        emit pairSelected(pairIndex);
    });

    // Le panneau peut être réduit jusqu'à sa seule barre d'onglets : il reste visible, mais ne prend presque plus de place.
    setMinimumHeight(tabBar()->sizeHint().height());
}

//affichage des paire en fonction des status 
void BottomTabs::updatePairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs)
{
    const QSignalBlocker blocker(m_pairsTable);
    const bool wasSorting = m_pairsTable->isSortingEnabled();
    m_pairsTable->setSortingEnabled(false);
    const int currentSelected = m_pairsTable->currentRow();
    m_pairsTable->setRowCount(pairs.size());

    for (int row = 0; row < pairs.size(); ++row) {
        const auto &pair = pairs.at(row);

        QString statusText = tr("fiable");
        QColor statusBg("#389e5c");
        QColor statusFg("#ffffff");
        if (pair.status == FrontendUtils::PairStatus::Dubious) {
            statusText = tr("douteuse");
            statusBg = QColor("#d9822b");
            statusFg = QColor("#1c1c1c");
        } else if (pair.status == FrontendUtils::PairStatus::Failed) {
            statusText = tr("échec");
            statusBg = QColor("#d94a38");
            statusFg = QColor("#ffffff");
        } else if (pair.status == FrontendUtils::PairStatus::Validated) {
            statusText = tr("validée");
            statusBg = QColor("#389e5c");
            statusFg = QColor("#ffffff");
        }

        const QString correctionText = (pair.intervention.isEmpty() || pair.intervention == tr("aucune"))
            ? QStringLiteral("—")
            : pair.intervention;

        const QStringList rowValues = {
            pair.pairLabel(),
            pair.direction,
            statusText,
            QString::number(pair.meanConfidence, 'f', 2).replace('.', ','),
            QString::number(pair.nccScore, 'f', 2).replace('.', ','),
            QString::number(pair.dx, 'f', 1).replace('.', ','),
            QString::number(pair.dy, 'f', 1).replace('.', ','),
            QStringLiteral("%1 / %2").arg(pair.inlierCount).arg(pair.totalMatches),
            tr("%1 px").arg(QString::number(pair.optimizationErrorPx, 'f', 2).replace('.', ',')),
            correctionText
        };

        for (int col = 0; col < rowValues.size(); ++col) {
            auto *item = new QTableWidgetItem(rowValues.at(col));
            if (col == 0) {
                item->setData(Qt::UserRole, row);
            } else if (col == 2) {
                item->setTextAlignment(Qt::AlignCenter);
                item->setBackground(QBrush(statusBg));
                item->setForeground(QBrush(statusFg));
                QFont boldFont = item->font();
                boldFont.setBold(true);
                item->setFont(boldFont);
            } else if (col == 8 && pair.isDubious()) {
                item->setForeground(QBrush(QColor("#e69138")));
                QFont boldFont = item->font();
                boldFont.setBold(true);
                item->setFont(boldFont);
            }
            m_pairsTable->setItem(row, col, item);
        }
    }

    if (m_pairsTabIndex >= 0) {
        setTabText(m_pairsTabIndex, tr("Paires (%1)").arg(pairs.size()));
    }

    if (!pairs.isEmpty()) {
        const int rowToSelect = qBound(0, currentSelected >= 0 ? currentSelected : 0, pairs.size() - 1);
        m_pairsTable->selectRow(rowToSelect);
    }
    m_pairsTable->setSortingEnabled(wasSorting);
}

//permet de mettre en évidence la paire sélectionnée dans le tableau du bas
void BottomTabs::selectPairRow(int pairIndex)
{
    if (!m_pairsTable || pairIndex < 0 || pairIndex >= m_pairsTable->rowCount())
        return;
    const QSignalBlocker blocker(m_pairsTable);
    for (int row = 0; row < m_pairsTable->rowCount(); ++row) {
        const auto *item = m_pairsTable->item(row, 0);
        if (item && item->data(Qt::UserRole).toInt() == pairIndex) {
            m_pairsTable->selectRow(row);
            return;
        }
    }
    m_pairsTable->selectRow(pairIndex);
}

void BottomTabs::appendLog(const QString &message)
{
    if (m_journalList) {
        QString timeStr = QTime::currentTime().toString("hh:mm:ss");
        m_journalList->addItem(timeStr + "  " + message);
        m_journalList->scrollToBottom();
    }
}
