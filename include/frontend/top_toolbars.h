// include/frontend/top_toolbars.h
#pragma once

#include <QWidget>

class MainWindow;
class ProjectGrid;
class PipelineRunnerClass;
class QComboBox;
class QHBoxLayout;
class QPushButton;

// Barre supérieure regroupant les modes d'affichage, la navigation dans les paires
// et les commandes de contrôle du calcul asynchrone (Lancer, Pause, Reprendre).
class TopToolbars : public QWidget
{
    Q_OBJECT
public:
    explicit TopToolbars(MainWindow *mainWindow, ProjectGrid *projectGrid, PipelineRunnerClass *pipelineRunner, QWidget *parent = nullptr);

    void updateTitle(const QString &projectName, const QString &modality);
    void updateDubiousState(int dubiousCount);
    void setExportVisible(bool visible);

    QComboBox *arrangementCombo() const { return m_arrangementCombo; }

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

    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    PipelineRunnerClass *m_pipelineRunner;

    QComboBox *m_arrangementCombo = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_exportButton = nullptr;
};