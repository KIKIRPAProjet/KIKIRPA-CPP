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

#include <QDateTime>
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
    // Le panneau peut être réduit jusqu'à sa seule barre d'onglets : il reste visible, mais ne prend presque plus de place.
    setMinimumHeight(tabBar()->sizeHint().height());
}

void BottomTabs::appendLog(const QString &message)
{
    if (m_journalList) {
        QString timeStr = QTime::currentTime().toString("hh:mm:ss");
        m_journalList->addItem(timeStr + "  " + message);
        m_journalList->scrollToBottom();
    }
}
