// include/frontend/assembly_tab.h
#pragma once

#include "project_setup_model.h"

#include <QWidget>

class MainWindow;
class ProjectGrid;
class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QVBoxLayout;

// Onglet latéral « Chaîne » : la méthode de chaque étape qui en propose plusieurs (tous les steps du registre qui
// peuvent la remplir), puis les étapes réelles du script envoyé au backend et la relance ciblée sur checkpoint.
class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, ProjectGrid *projectGrid, QWidget *parent = nullptr);

    // Met en surbrillance l'étape actuellement en cours d'exécution dans le pipeline (1-indexée).
    void setActiveStep(int stepNumber);

    // Affiche les méthodes du projet (ouverture, autre onglet) et la liste d'étapes qui en découle. LightGlue sans
    // modèle sur ce poste est remplacé par la méthode par défaut, dans le projet aussi.
    void updateChoices(const FrontendNs::PipelineChoices &requestedChoices);

private:
    // Construit les menus de méthodes, puis la liste des étapes et le bouton de relance ciblée.
    void setupMethodControls(QVBoxLayout *layout);
    QComboBox *addStage(const QString &title);
    void setupStepList(QVBoxLayout *layout);

    // Remplit la liste des étapes pour ces méthodes.
    void refreshStepList(const FrontendNs::PipelineChoices &choices);

    // Note « Vote 2D automatique », affichée seulement avec SIFT et LightGlue.
    void refreshVoteNote(const FrontendNs::PipelineChoices &choices);

    // Lit les méthodes affichées et les enregistre dans le projet.
    void applyChoices();

    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    QFormLayout *m_methodsForm = nullptr;
    QListWidget *m_stepList = nullptr;
    QComboBox *m_overlapCombo = nullptr;
    QComboBox *m_matchingCombo = nullptr;
    QLabel *m_voteNote = nullptr;
    QComboBox *m_intensityCombo = nullptr;
    QComboBox *m_seamCombo = nullptr;
    QComboBox *m_verificationCombo = nullptr;
    bool m_isLightGlueAvailable = false;   // modèle présent à côté de l'exécutable (FrontendUtils::lightGlueModelPath)
    int m_activeStep = 0;
};
