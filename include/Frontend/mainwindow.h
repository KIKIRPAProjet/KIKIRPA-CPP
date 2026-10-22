#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QStringList>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QWidget;
class QKeyEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    void keyPressEvent(QKeyEvent *event) override;
    void createHomePage();
    void createWorkspace();
    void createProject();
    void setMode(int mode);
    void refreshProjectInfo();
    void importTiles();
    void saveProject();
    void openProject();
    void startRun(int firstStep);
    void showComparison();
    void showExportDialog();
    void updateStatus(const QString &message);

    QStackedWidget *pages = nullptr;
    QWidget *workspace = nullptr;
    QWidget *canvas = nullptr;
    QTabWidget *rightTabs = nullptr;
    QTabWidget *bottomTabs = nullptr;
    QTableWidget *pairsTable = nullptr;
    QLabel *projectNameLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *zoomLabel = nullptr;
    QLabel *tileCountLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QComboBox *arrangementCombo = nullptr;
    QString projectName = QStringLiteral("VanDerVeken");
    QString projectPath;
    QString tileDirectory;
    QString outputDirectory;
    QStringList tilePaths;
    int currentMode = 4;
    int zoomPercent = 12;
    bool panelsVisible = true;
    bool maskVisible = true;
    bool comparisonVisible = false;
};

#endif
