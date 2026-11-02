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
class QHBoxLayout;
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

struct GridTile
{
    QString path;
    int rotation = 0;
    bool flipped = false;
    bool empty = false;
};

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
    void createHomePage();
    void createWorkspace();
    void createProject();
    void refreshRecentProjects();
    void rememberRecentProject(const QString &path);
    bool loadProjectFromPath(const QString &path);
    void refreshGrid();
    void refreshGridControls();
    void initializeGridFromPaths();
    void swapGridTiles(int first, int second);
    void importReferenceImage();
    void compareReferenceImage();
    void validateGrid();
    void setMode(int mode);
    void refreshProjectInfo();
    void importTiles();
    void saveProject();
    void openProject();
    void startRun(int firstStep);
    void showComparison();
    void showExportDialog();
    void updateStatus(const QString &message);
    void refreshProgressVisibility();

    QStackedWidget *pages = nullptr;
    QWidget *workspace = nullptr;
    QWidget *canvas = nullptr;
    QWidget *projectInfoPanel = nullptr;
    QTabWidget *rightTabs = nullptr;
    QTabWidget *bottomTabs = nullptr;
    QWidget *gridTabPage = nullptr;
    QHBoxLayout *recentCardsLayout = nullptr;
    QLabel *recentEmptyLabel = nullptr;
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
    QString projectName = QStringLiteral("VanDerVeken");
    QString projectPath;
    QString tileDirectory;
    QString outputDirectory;
    QStringList tilePaths;
    QVector<GridTile> gridTiles;
    QString referenceImagePath;
    int gridRows = 3;
    int gridColumns = 2;
    int captureOrder = 0;
    int selectedGridCell = 0;
    int horizontalOverlap = 20;
    int verticalOverlap = 20;
    bool overlapsLinked = true;
    QString modality = QStringLiteral("Infrarouge");
    double scaleMmPerPixel = 0.0;
    double uncertaintyMargin = 0.0;
    bool automaticUncertainty = true;
    bool gridValidated = false;
    bool projectCreated = false;
    int currentMode = 4;
    int zoomPercent = 12;
    bool panelsVisible = true;
    bool maskVisible = true;
    bool comparisonVisible = false;
    bool progressRunning = false;
};

#endif
