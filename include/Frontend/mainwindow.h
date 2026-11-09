#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QString>
#include <QStringList>

class QKeyEvent;
class QWidget;
class ProjectGrid;
class SimulationRunner;
class WorkspaceUI;
class MenuBarManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

public slots:
    void updateProgress(const QString &stepName, int percentage);

private:
    friend class WorkspaceUI;
    friend class GridTab;
    friend class AssemblyTab;
    friend class AlignmentTab;
    friend class MasksTab;

    void keyPressEvent(QKeyEvent *event) override;
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
    void applyLoadedProject(const QString &path);
    void openCurrentProjectFromHome();
    void handleTilesImported(const QString &directory, int importedCount);
    void showGridMessage(const QString &title, const QString &message, bool warning);
    void handleGridValidation(bool valid, int activeTileCount, const QStringList &errors);
    void showComparisonDialog();
    void showCancelRunButton();
    void removeCancelRunButton();

private:
    ProjectGrid *projectGrid = nullptr;
    SimulationRunner *simulationRunner = nullptr;
    WorkspaceUI *workspaceUI = nullptr;
    MenuBarManager *menuBarManager = nullptr;
    QString projectName = QStringLiteral("VanDerVeken");
    QString projectPath;
    QString outputDirectory;
    bool projectCreated = false;
    int currentMode = 4;
    int zoomPercent = 12;
    bool panelsVisible = true;
    bool maskVisible = true;
    QImage finalMosaic;
};

#endif
