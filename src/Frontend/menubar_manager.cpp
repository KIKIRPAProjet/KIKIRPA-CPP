/*
 * Header : le QUOI
 * Construit la barre de menus et ses actions.
 * - ce que ça fait : crée les menus, associe les raccourcis et connecte les actions aux signaux ;
 * - ce que ça renvoie : aucune valeur directement, les demandes sont émises sous forme de signaux ;
 * - quand ça lève une exception : si la création d'un menu ou d'une action échoue.
 */
//  construit la barre de menus et ses actions
#include "menubar_manager.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

MenuBarManager::MenuBarManager(QObject *parent)
    : QObject(parent)
{
}

void MenuBarManager::setupMenus(QMenuBar *menuBar)
{
    auto *fileMenu = menuBar->addMenu(tr("Fichier"));
    auto *newAction = fileMenu->addAction(tr("Nouveau projet…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MenuBarManager::newProjectRequested);
    auto *openAction = fileMenu->addAction(tr("Ouvrir un projet…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MenuBarManager::openProjectRequested);
    auto *importAction = fileMenu->addAction(tr("Importer un dossier de photos…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MenuBarManager::importTilesRequested);
    auto *saveAction = fileMenu->addAction(tr("Enregistrer le projet"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MenuBarManager::saveProjectRequested);
    fileMenu->addSeparator();
    auto *exportAction = fileMenu->addAction(tr("Exporter / rapport…"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MenuBarManager::exportRequested);
    fileMenu->addSeparator();
    connect(fileMenu->addAction(tr("Retour à l’accueil")),
            &QAction::triggered, this, &MenuBarManager::homeRequested);
    auto *quitAction = fileMenu->addAction(tr("Quitter"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MenuBarManager::quitRequested);

    auto *editMenu = menuBar->addMenu(tr("Édition"));
    connect(editMenu->addAction(tr("Annuler"), QKeySequence::Undo),
            &QAction::triggered, this, &MenuBarManager::undoRequested);
    connect(editMenu->addAction(tr("Rétablir"), QKeySequence::Redo),
            &QAction::triggered, this, &MenuBarManager::redoRequested);

    auto *viewMenu = menuBar->addMenu(tr("Affichage"));
    connect(viewMenu->addAction(tr("Afficher / masquer les panneaux")),
            &QAction::triggered, this, &MenuBarManager::togglePanelsRequested);
    auto *zoomIn = viewMenu->addAction(tr("Zoom avant"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, &MenuBarManager::zoomInRequested);
    auto *zoomOut = viewMenu->addAction(tr("Zoom arrière"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, &MenuBarManager::zoomOutRequested);
    auto *fit = viewMenu->addAction(tr("Ajuster à la fenêtre"));
    fit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fit, &QAction::triggered, this, &MenuBarManager::fitToWindowRequested);

    auto *tilesMenu = menuBar->addMenu(tr("Tuiles"));
    connect(tilesMenu->addAction(tr("Importer un dossier de photos…")),
            &QAction::triggered, this, &MenuBarManager::importTilesRequested);
    connect(tilesMenu->addAction(tr("Organiser la grille")),
            &QAction::triggered, this, [this]() { emit modeChangeRequested(1); });

    auto *masksMenu = menuBar->addMenu(tr("Masques"));
    connect(masksMenu->addAction(tr("Détecter les bords noirs")),
            &QAction::triggered, this, [this]() { emit modeChangeRequested(2); });

    auto *assemblyMenu = menuBar->addMenu(tr("Assemblage"));
    connect(assemblyMenu->addAction(tr("Tout lancer"), QKeySequence(Qt::Key_F5)),
            &QAction::triggered, this, [this]() { emit runRequested(1); });
    connect(assemblyMenu->addAction(tr("Relancer à partir de…")),
            &QAction::triggered, this, [this]() { emit runRequested(6); });
    connect(assemblyMenu->addAction(tr("Comparer deux exécutions…")),
            &QAction::triggered, this, &MenuBarManager::comparisonRequested);

    auto *helpMenu = menuBar->addMenu(tr("Aide"));
    connect(helpMenu->addAction(tr("À propos")),
            &QAction::triggered, this, &MenuBarManager::aboutRequested);
}
