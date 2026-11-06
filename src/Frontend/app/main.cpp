/*
 * .cpp : le POURQUOI
 * - choix de conception : utiliser QApplication permet de gérer la boucle d'événements Qt ;
 * - cas limite : l'application doit rester active tant que la fenêtre principale est ouverte ;
 * - unité ou repère : le code de sortie est celui attendu par le système d'exploitation ;
 * - raison non évidente : QApplication doit être créée avant tout widget Qt.
 */
/*
 * Header : le QUOI
 * Initialise l'application graphique et affiche la fenêtre principale.
 * - ce que ça fait : crée l'application Qt, construit la fenêtre principale et l'affiche ;
 * - ce que ça renvoie : le code de sortie fourni par la boucle d'événements Qt ;
 * - quand ça lève une exception : si l'initialisation de l'application ou de la fenêtre échoue.
 */
// lance application graphique
#include "Frontend/app/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
