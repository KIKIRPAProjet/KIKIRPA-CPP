// include/frontend/assembly_tab.h
#pragma once

#include <QWidget>

class MainWindow;
class QListWidget;
class QVBoxLayout;

// Onglet latéral affichant la chaîne de traitement (les 5 étapes réelles du backend)
// et offrant le point d'entrée pour la relance ciblée sur checkpoint.
class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, QWidget *parent = nullptr);

    // Met en surbrillance l'étape actuellement en cours d'exécution dans le pipeline (1-indexée).
    void setActiveStep(int stepNumber);

private:
    // Construit la liste visuelle ordonnée des 5 étapes algorithmiques.
    void setupStepList(QVBoxLayout *layout);

    // Initialise les boutons d'action (relance ciblée, choix d'algorithme).
    void setupActionControls(QVBoxLayout *layout);

    MainWindow *m_mainWindow;
    QListWidget *m_stepList = nullptr;
    int m_activeStep = 0;
};