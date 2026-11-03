/*
 * Header : le QUOI
 * Initialise l'application graphique et affiche la fenêtre principale.
 * - ce que ça fait : crée l'application Qt, construit la fenêtre principale et l'affiche ;
 * - ce que ça renvoie : le code de sortie fourni par la boucle d'événements Qt ;
 * - quand ça lève une exception : si l'initialisation de l'application ou de la fenêtre échoue.
 */
// lance application graphique
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
