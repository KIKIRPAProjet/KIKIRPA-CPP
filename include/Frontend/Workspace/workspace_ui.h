#ifndef WORKSPACE_UI_H
#define WORKSPACE_UI_H

#include "projectgrid.h"
#include "grid_tab.h"
#include "alignment_tab.h"

#include <QObject>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QWidget;
class HomePage;
class LogPanel;
class MainWindow;
class SimulationRunner;

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
    QWidget *canvas() const { return m_canvas; }
    QWidget *projectInfoPanel() const { return m_projectInfoPanel; }
    QTabWidget *rightTabs() const { return m_rightTabs; }
    QTabWidget *bottomTabs() const { return m_bottomTabs; }
    QWidget *gridTabPage() const { return m_gridTab; }
    QComboBox *captureOrderCombo() const
    {
        return m_gridTab ? m_gridTab->captureOrderCombo() : nullptr;
    }
    
    GridTab *gridtab() const { return m_gridTab; }
    
    QTableWidget *pairsTable() const { return m_pairsTable; }
    QLineEdit *projectNameEdit() const { return m_projectNameEdit; }
    QLabel *projectFolderLabel() const { return m_projectFolderLabel; }
    QLabel *projectFormatLabel() const { return m_projectFormatLabel; }
    QLabel *statusLabel() const { return m_statusLabel; }
    QLabel *currentStepLabel() const { return m_currentStepLabel; }
    QLabel *zoomLabel() const { return m_zoomLabel; }
    QProgressBar *progressBar() const { return m_progressBar; }
    QComboBox *arrangementCombo() const { return m_arrangementCombo; }
    QPushButton *cancelRunButton() const { return m_cancelRunButton; }
    
    void setCancelRunButton(QPushButton *button) { m_cancelRunButton = button; }
    void setActiveStep(int stepNumber);
    void appendLog(const QString &message);

private:
    
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void createWorkspace();


    QWidget *createToolbar();
    QWidget *createModesBar();
    QWidget *createOptionsBar();
    QWidget *createLeftToolbar();
    QWidget *createCenterArea();
    QTabWidget *createRightTabs();
    QTabWidget *createBottomTabs();
    QWidget *createStatusBar();
 
    void createProjectMode(QWidget *center);

    MainWindow *m_mainWindow = nullptr;
    ProjectGrid *m_projectGrid = nullptr;
    SimulationRunner *m_simulationRunner = nullptr;
    HomePage *m_homePage = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_workspace = nullptr;
    QWidget *m_canvas = nullptr;
    QWidget *m_projectInfoPanel = nullptr;
    QTabWidget *m_rightTabs = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    
    GridTab *m_gridTab = nullptr;
    AlignmentTab *m_alignmentTab = nullptr;

    QTableWidget *m_pairsTable = nullptr;
    QLineEdit *m_projectNameEdit = nullptr;
    QLabel *m_projectFolderLabel = nullptr;
    QLabel *m_projectFormatLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_currentStepLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    LogPanel *m_logPanel = nullptr;
    QComboBox *m_arrangementCombo = nullptr;
    QPushButton *m_cancelRunButton = nullptr;
    QListWidget *m_journalList = nullptr;
    int m_activeStep = 0;
};

#endif
