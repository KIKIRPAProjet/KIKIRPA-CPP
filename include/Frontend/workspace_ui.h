#ifndef WORKSPACE_UI_H
#define WORKSPACE_UI_H

#include "projectgrid.h"

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
    QWidget *gridTabPage() const { return m_gridTabPage; }
    QTableWidget *pairsTable() const { return m_pairsTable; }
    QLabel *projectNameLabel() const { return m_projectNameLabel; }
    QLineEdit *projectNameEdit() const { return m_projectNameEdit; }
    QLabel *projectFolderLabel() const { return m_projectFolderLabel; }
    QLabel *projectFormatLabel() const { return m_projectFormatLabel; }
    QLabel *statusLabel() const { return m_statusLabel; }
    QLabel *currentStepLabel() const { return m_currentStepLabel; }
    QLabel *zoomLabel() const { return m_zoomLabel; }
    QLabel *tileCountLabel() const { return m_tileCountLabel; }
    QProgressBar *progressBar() const { return m_progressBar; }
    QComboBox *arrangementCombo() const { return m_arrangementCombo; }
    QSpinBox *gridRowsSpin() const { return m_gridRowsSpin; }
    QSpinBox *gridColumnsSpin() const { return m_gridColumnsSpin; }
    QSpinBox *horizontalOverlapSpin() const { return m_horizontalOverlapSpin; }
    QSpinBox *verticalOverlapSpin() const { return m_verticalOverlapSpin; }
    QComboBox *captureOrderCombo() const { return m_captureOrderCombo; }
    QComboBox *modalityCombo() const { return m_modalityCombo; }
    QDoubleSpinBox *scaleSpin() const { return m_scaleSpin; }
    QDoubleSpinBox *uncertaintySpin() const { return m_uncertaintySpin; }
    QCheckBox *linkedOverlapCheck() const { return m_linkedOverlapCheck; }
    QCheckBox *automaticUncertaintyCheck() const { return m_automaticUncertaintyCheck; }
    QPushButton *emptyCellButton() const { return m_emptyCellButton; }
    QLabel *selectedGridTileLabel() const { return m_selectedGridTileLabel; }
    QLabel *gridSummaryLabel() const { return m_gridSummaryLabel; }
    QLabel *referencePathLabel() const { return m_referencePathLabel; }
    QPushButton *cancelRunButton() const { return m_cancelRunButton; }
    void setCancelRunButton(QPushButton *button) { m_cancelRunButton = button; }
    void setActiveStep(int stepNumber);
    void appendLog(const QString &message);

private:
    
    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void updateGridParameters(const ProjectGrid::State &gridState);
    void updateGridSummary(const ProjectGrid::State &gridState);
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
    QWidget *createTilesMode();
    QWidget *createMasksMode();
    QWidget *createAlignmentMode();
    QWidget *createAssemblyMode();

    void showManualAdjustmentDialog();
    
    QWidget *createGridDimensionsGroup(QWidget *parent);
    QWidget *createGridCaptureGroup(QWidget *parent);
    void connectTilesModeSignals(QPushButton *rotateTile, QPushButton *flipTile,
                                 QPushButton *importReference, QPushButton *compareReference,
                                 QPushButton *validateGridButton);

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
    QWidget *m_gridTabPage = nullptr;
    QTableWidget *m_pairsTable = nullptr;
    QListWidget *m_stepList = nullptr;
    QLabel *m_projectNameLabel = nullptr;
    QLineEdit *m_projectNameEdit = nullptr;
    QLabel *m_projectFolderLabel = nullptr;
    QLabel *m_projectFormatLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_currentStepLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_tileCountLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    LogPanel *m_logPanel = nullptr;
    QComboBox *m_arrangementCombo = nullptr;
    QSpinBox *m_gridRowsSpin = nullptr;
    QSpinBox *m_gridColumnsSpin = nullptr;
    QSpinBox *m_horizontalOverlapSpin = nullptr;
    QSpinBox *m_verticalOverlapSpin = nullptr;
    QComboBox *m_captureOrderCombo = nullptr;
    QComboBox *m_modalityCombo = nullptr;
    QDoubleSpinBox *m_scaleSpin = nullptr;
    QDoubleSpinBox *m_uncertaintySpin = nullptr;
    QCheckBox *m_linkedOverlapCheck = nullptr;
    QCheckBox *m_automaticUncertaintyCheck = nullptr;
    QPushButton *m_emptyCellButton = nullptr;
    QLabel *m_selectedGridTileLabel = nullptr;
    QLabel *m_gridSummaryLabel = nullptr;
    QLabel *m_referencePathLabel = nullptr;
    QPushButton *m_cancelRunButton = nullptr;
    QListWidget *m_journalList = nullptr;
    int m_activeStep = 0;
};

#endif
