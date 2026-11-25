#pragma once

#include "image_layer.h"
#include "projectgrid.h"

#include <QObject>
#include <QVector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QWidget;
class HomePage;
class MainWindow;
class PipelineRunnerClass;
class ArtworkCanvas;
class LogPanel;
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
                PipelineRunnerClass *pipelineRunner);

    void updateFromGridState(const QString &projectName,
                             const ProjectGrid::State &gridState);
    void setImageLayers(const QVector<ImageLayer> &layers, const QRectF &bounds = {});

    HomePage *homePage() const { return m_homePage; }
    QStackedWidget *pages() const { return m_pages; }
    QWidget *workspace() const { return m_workspace; }

    // Les accesseurs ci-dessous déléguent aux sous-composants.
    QWidget *canvas() const;
    QWidget *projectInfoPanel() const;
    QLabel *zoomLabel() const;
    QTabWidget *rightTabs() const;
    LogPanel *logPanel() const { return m_logPanel; }
    RightPanel *rightPanel() const { return m_rightPanel; }
    TopToolbars *topToolbars() const { return m_topToolbars; }
    LeftToolbar *leftToolbar() const { return m_leftToolbar; }
    WorkspaceStatusBar *workspaceStatusBar() const { return m_statusBar; }
    QWidget *gridTabPage() const;
    GridTab *gridtab() const;
    QLabel *statusLabel() const;
    QLabel *currentStepLabel() const;
    QProgressBar *progressBar() const;

    QPushButton *cancelRunButton() const { return m_cancelRunButton; }
    void setCancelRunButton(QPushButton *button) { m_cancelRunButton = button; }
    void appendLog(const QString &message);
    void setActiveStep(int stepNumber);
    // Affiche l'état sauvegardé dans l'onglet Chaîne (voir AssemblyTab::showSavedState).
    void showSavedState(int savedStepCount);

private:
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void createWorkspace();


    MainWindow *m_mainWindow = nullptr;
    ProjectGrid *m_projectGrid = nullptr;
    PipelineRunnerClass *m_pipelineRunner = nullptr;
    HomePage *m_homePage = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_workspace = nullptr;
    LogPanel *m_logPanel = nullptr;
    WorkspaceStatusBar *m_statusBar = nullptr;
    WorkspaceCenter *m_centerArea = nullptr;
    LeftToolbar *m_leftToolbar = nullptr;
    RightPanel *m_rightPanel = nullptr;
    TopToolbars *m_topToolbars = nullptr;
    QPushButton *m_cancelRunButton = nullptr;
};
