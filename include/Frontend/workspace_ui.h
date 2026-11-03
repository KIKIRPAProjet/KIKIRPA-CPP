#ifndef WORKSPACE_UI_H
#define WORKSPACE_UI_H

#include "projectgrid.h"

#include <QObject>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
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

    HomePage *getHomePage() const { return homePage; }
    QStackedWidget *getPages() const { return pages; }
    QWidget *getWorkspace() const { return workspace; }
    QWidget *getCanvas() const { return canvas; }
    QWidget *getProjectInfoPanel() const { return projectInfoPanel; }
    QTabWidget *getRightTabs() const { return rightTabs; }
    QTabWidget *getBottomTabs() const { return bottomTabs; }
    QWidget *getGridTabPage() const { return gridTabPage; }
    QTableWidget *getPairsTable() const { return pairsTable; }
    QLabel *getProjectNameLabel() const { return projectNameLabel; }
    QLineEdit *getProjectNameEdit() const { return projectNameEdit; }
    QLabel *getProjectFolderLabel() const { return projectFolderLabel; }
    QLabel *getProjectFormatLabel() const { return projectFormatLabel; }
    QLabel *getStatusLabel() const { return statusLabel; }
    QLabel *getCurrentStepLabel() const { return currentStepLabel; }
    QLabel *getZoomLabel() const { return zoomLabel; }
    QLabel *getTileCountLabel() const { return tileCountLabel; }
    QProgressBar *getProgressBar() const { return progressBar; }
    QComboBox *getArrangementCombo() const { return arrangementCombo; }
    QSpinBox *getGridRowsSpin() const { return gridRowsSpin; }
    QSpinBox *getGridColumnsSpin() const { return gridColumnsSpin; }
    QSpinBox *getHorizontalOverlapSpin() const { return horizontalOverlapSpin; }
    QSpinBox *getVerticalOverlapSpin() const { return verticalOverlapSpin; }
    QComboBox *getCaptureOrderCombo() const { return captureOrderCombo; }
    QComboBox *getModalityCombo() const { return modalityCombo; }
    QDoubleSpinBox *getScaleSpin() const { return scaleSpin; }
    QDoubleSpinBox *getUncertaintySpin() const { return uncertaintySpin; }
    QCheckBox *getLinkedOverlapCheck() const { return linkedOverlapCheck; }
    QCheckBox *getAutomaticUncertaintyCheck() const { return automaticUncertaintyCheck; }
    QPushButton *getEmptyCellButton() const { return emptyCellButton; }
    QLabel *getSelectedGridTileLabel() const { return selectedGridTileLabel; }
    QLabel *getGridSummaryLabel() const { return gridSummaryLabel; }
    QLabel *getReferencePathLabel() const { return referencePathLabel; }
    QPushButton *getCancelRunButton() const { return cancelRunButton; }
    void setCancelRunButton(QPushButton *button) { cancelRunButton = button; }

private:
    void createWorkspace();
    void createProjectMode(QWidget *center);
    QWidget *createTilesMode();
    QWidget *createMasksMode();
    QWidget *createAlignmentMode();
    QWidget *createAssemblyMode();

    MainWindow *mainWindow = nullptr;
    ProjectGrid *projectGrid = nullptr;
    SimulationRunner *simulationRunner = nullptr;
    HomePage *homePage = nullptr;
    QStackedWidget *pages = nullptr;
    QWidget *workspace = nullptr;
    QWidget *canvas = nullptr;
    QWidget *projectInfoPanel = nullptr;
    QTabWidget *rightTabs = nullptr;
    QTabWidget *bottomTabs = nullptr;
    QWidget *gridTabPage = nullptr;
    QTableWidget *pairsTable = nullptr;
    QLabel *projectNameLabel = nullptr;
    QLineEdit *projectNameEdit = nullptr;
    QLabel *projectFolderLabel = nullptr;
    QLabel *projectFormatLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *currentStepLabel = nullptr;
    QLabel *zoomLabel = nullptr;
    QLabel *tileCountLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    LogPanel *logPanel = nullptr;
    QComboBox *arrangementCombo = nullptr;
    QSpinBox *gridRowsSpin = nullptr;
    QSpinBox *gridColumnsSpin = nullptr;
    QSpinBox *horizontalOverlapSpin = nullptr;
    QSpinBox *verticalOverlapSpin = nullptr;
    QComboBox *captureOrderCombo = nullptr;
    QComboBox *modalityCombo = nullptr;
    QDoubleSpinBox *scaleSpin = nullptr;
    QDoubleSpinBox *uncertaintySpin = nullptr;
    QCheckBox *linkedOverlapCheck = nullptr;
    QCheckBox *automaticUncertaintyCheck = nullptr;
    QPushButton *emptyCellButton = nullptr;
    QLabel *selectedGridTileLabel = nullptr;
    QLabel *gridSummaryLabel = nullptr;
    QLabel *referencePathLabel = nullptr;
    QPushButton *cancelRunButton = nullptr;
};

#endif
