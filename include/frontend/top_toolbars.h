#ifndef TOP_TOOLBARS_H
#define TOP_TOOLBARS_H

#include <QWidget>

class MainWindow;
class ProjectGrid;
class SimulationRunner;
class QComboBox;
class QPushButton;

class TopToolbars : public QWidget
{
    Q_OBJECT
public:
    explicit TopToolbars(MainWindow *mainWindow, ProjectGrid *projectGrid, SimulationRunner *simulationRunner, QWidget *parent = nullptr);

    void updateTitle(const QString &projectName, const QString &modality);
    // Met à jour l'accessibilité des boutons Lancer / Reprendre selon l'état du pipeline.
    void setResumeAvailable(bool available);
    void setRunInProgress(bool running);

    QComboBox *arrangementCombo() const { return m_arrangementCombo; }

private:
    void createToolbar();
    void createModesBar();
    void createOptionsBar();

    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    SimulationRunner *m_simulationRunner;

    QComboBox *m_arrangementCombo = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
    bool m_resumeAvailable = false;
    bool m_runInProgress = false;
};

#endif 