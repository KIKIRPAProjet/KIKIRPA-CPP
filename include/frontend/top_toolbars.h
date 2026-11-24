// include/frontend/top_toolbars.h
#pragma once

#include <QWidget>

class MainWindow;
class ProjectGrid;
class PipelineRunnerClass;
class QHBoxLayout;
class QPushButton;
class QButtonGroup;

// Barre supérieure regroupant les modes d'affichage, la navigation dans les paires
// et les commandes de contrôle du calcul asynchrone (Lancer, Pause, Reprendre).
class TopToolbars : public QWidget
{
    Q_OBJECT
public:
    explicit TopToolbars(MainWindow *mainWindow, ProjectGrid *projectGrid, PipelineRunnerClass *pipelineRunner, QWidget *parent = nullptr);

    void updateTitle(const QString &projectName, const QString &modality);
    void updateDubiousState(int dubiousCount);
    // Affiche ou masque le bouton d'exportation de la mosaïque finale selon sa disponibilité.
    void setExportVisible(bool visible);

    // Active ou désactive l'accès à un mode donné dans la barre supérieure.
    void setModeEnabled(int modeIndex, bool enabled);
    // Indique si le bouton correspondant au mode est actuellement cliquable.
    bool isModeEnabled(int modeIndex) const;
    // Réinitialise la disponibilité : seul le mode 0 (Projet) reste actif.
    void resetModesAvailability();

signals:
    void previousPairRequested();
    void nextDubiousPairRequested();

public slots:
    // Aligne l'intitulé du bouton principal (Lancer / Pause / Reprendre) sur l'état du runner.
    void updateRunButtonState();

private:
    void createModesBar();
    void createModeButtons(QWidget *stages, QHBoxLayout *stageLayout);
    void createActionButtons(QWidget *stages, QHBoxLayout *stageLayout);
    void createOptionsBar();

    // Réagit au clic sur le bouton principal selon qu'on soit en cours de calcul, en pause ou au repos.
    void handleRunButtonClicked();

    QButtonGroup *m_modeGroup = nullptr;

    MainWindow *m_mainWindow;
    PipelineRunnerClass *m_pipelineRunner;

    QPushButton *m_runButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_exportButton = nullptr;
};
