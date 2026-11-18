// include/frontend/assembly_tab.h
#pragma once

#include "project_setup_model.h"

#include <QWidget>

class MainWindow;
class ProjectGrid;
class QCheckBox;
class QComboBox;
class QListWidget;
class QVBoxLayout;

// Onglet latéral affichant la chaîne de traitement (les étapes réelles du script envoyé au backend), le choix de la
// méthode de chaque étape, et le point d'entrée pour la relance ciblée sur checkpoint.
class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, ProjectGrid *projectGrid, QWidget *parent = nullptr);

    // Met en surbrillance l'étape actuellement en cours d'exécution dans le pipeline (1-indexée).
    void setActiveStep(int stepNumber);

    // Affiche les méthodes du projet (ouverture, autre onglet) et la liste d'étapes qui en découle.
    void updateChoices(const FrontendNs::PipelineChoices &choices);

private:
    // Construit la liste visuelle ordonnée des étapes du script.
    void setupStepList(QVBoxLayout *layout);

    // Initialise les boutons d'action (relance ciblée) et le choix des méthodes.
    void setupActionControls(QVBoxLayout *layout);
    void setupMethodControls(QVBoxLayout *layout);

    // Remplit la liste des étapes pour ces méthodes.
    void refreshStepList(const FrontendNs::PipelineChoices &choices);

    // Lit les méthodes affichées et les enregistre dans le projet.
    void applyChoices();

    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    QListWidget *m_stepList = nullptr;
    QCheckBox *m_overlapCheck = nullptr;
    QComboBox *m_matchingCombo = nullptr;
    QCheckBox *m_voteCheck = nullptr;
    QComboBox *m_intensityCombo = nullptr;
    QComboBox *m_seamCombo = nullptr;
    int m_activeStep = 0;
};
