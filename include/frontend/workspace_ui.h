#ifndef WORKSPACE_UI_H
#define WORKSPACE_UI_H

#include "projectgrid.h"

#include <QObject>
#include <QStringList>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QWidget;
class HomePage;
class MainWindow;
class SimulationRunner;
class ArtworkCanvas;
class BottomTabs;
class GridTab;
class WorkspaceStatusBar;
class WorkspaceCenter;
class LeftToolbar;
class RightPanel;
class TopToolbars;

class WorkspaceUI : public QObject
{
    Q_OBJECT

public:
    WorkspaceUI(MainWindow *mainWindow, HomePage *homePage, ProjectGrid *projectGrid,
                SimulationRunner *simulationRunner);

    void updateFromGridState(const QString &projectName,
                             const ProjectGrid::State &gridState);

    HomePage *homePage() const { return m_homePage; }
    QStackedWidget *pages() const { return m_pages; }
    QWidget *workspace() const { return m_workspace; }

    // Les accesseurs ci-dessous déléguent aux sous-composants.
    QWidget *canvas() const;
    QWidget *projectInfoPanel() const;
    QLabel *zoomLabel() const;
    QTabWidget *rightTabs() const;
    QTabWidget *bottomTabs() const;
    QWidget *gridTabPage() const;
    GridTab *gridtab() const;
    QComboBox *captureOrderCombo() const;
    QComboBox *arrangementCombo() const;
    QLabel *statusLabel() const;
    QLabel *currentStepLabel() const;
    QProgressBar *progressBar() const;

    QPushButton *cancelRunButton() const { return m_cancelRunButton; }
    void setCancelRunButton(QPushButton *button) { m_cancelRunButton = button; }
    void appendLog(const QString &message);
    void setActiveStep(int stepNumber);
    // Affiche l'état sauvegardé dans l'onglet Chaîne (voir AssemblyTab::showSavedState).
    void showSavedState(const QStringList &stepNames, int savedStepCount);

private:
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void createWorkspace();


    MainWindow *m_mainWindow = nullptr;
    ProjectGrid *m_projectGrid = nullptr;
    SimulationRunner *m_simulationRunner = nullptr;
    HomePage *m_homePage = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_workspace = nullptr;
    BottomTabs *m_bottomTabs = nullptr;
    WorkspaceStatusBar *m_statusBar = nullptr;
    WorkspaceCenter *m_centerArea = nullptr;
    LeftToolbar *m_leftToolbar = nullptr;
    RightPanel *m_rightPanel = nullptr;
    TopToolbars *m_topToolbars = nullptr;
    QPushButton *m_cancelRunButton = nullptr;
};

#endif // WORKSPACE_UI_H