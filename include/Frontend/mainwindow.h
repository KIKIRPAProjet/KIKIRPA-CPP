#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QCheckBox;
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
class QKeyEvent;
class LogPanel;
class HomePage;
class ProjectGrid;
class SimulationRunner;
class QJsonObject;
class QImage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

public slots:
    void updateProgress(const QString &stepName, int percentage);

private:
    void keyPressEvent(QKeyEvent *event) override;
    void createWorkspace();
    void createProjectMode(QWidget *center);
    QWidget *createTilesMode();
    QWidget *createMasksMode();
    QWidget *createAlignmentMode();
    QWidget *createAssemblyMode();
    void refreshGrid();
    void refreshGridControls();
    void chooseTilesDirectory();
    bool chooseReferenceImage();
    void showReferenceComparison(const QString &referencePath, const QString &tilePath,
                                 const QImage &referenceImage, const QImage &tileImage);
    void setMode(int mode);
    void refreshProjectInfo();
    void saveProject();
    void requestRun(int firstStep);
    void showExportDialog();
    void updateStatus(const QString &message);
    void refreshProgressVisibility();

private slots:
    void initializeNewProject(const QString &name, const QString &tileDirectory,
                              const QStringList &tilePaths);
    void applyLoadedProject(const QString &path, const QJsonObject &project);
    void openCurrentProjectFromHome();
    void handleTilesImported(const QString &directory, int importedCount);
    void showGridMessage(const QString &title, const QString &message, bool warning);
    void handleGridValidation(bool valid, int activeTileCount, const QStringList &errors);
    void showComparisonDialog();
    void showCancelRunButton();
    void removeCancelRunButton();

private:
    QStackedWidget *pages = nullptr;
    HomePage *homePage = nullptr;
    ProjectGrid *projectGrid = nullptr;
    SimulationRunner *simulationRunner = nullptr;
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
    QString projectName = QStringLiteral("VanDerVeken");
    QString projectPath;
    QString outputDirectory;
    bool projectCreated = false;
    int currentMode = 4;
    int zoomPercent = 12;
    bool panelsVisible = true;
    bool maskVisible = true;
};

#endif
