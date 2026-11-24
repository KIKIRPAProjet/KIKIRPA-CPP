// include/frontend/mainwindow.h
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include "frontendutils.h"
#include "project_setup_model.h"

class QCloseEvent;
class QKeyEvent;
class QWidget;
class ProjectGrid;
class PipelineRunnerClass;
class WorkspaceUI;
class MenuBarManager;
class QImage;
struct PipelineResults;

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
    friend class BottomTabs;
    friend class LeftToolbar;
    friend class RightPanel;
    friend class TopToolbars;
    friend class WorkspaceCenter;
    friend class WorkspaceStatusBar;
    QString mosaicFilePath;

    void keyPressEvent(QKeyEvent *event) override;

    // Intercepte la fermeture de la fenêtre pour interrompre proprement un éventuel calcul en cours.
    void closeEvent(QCloseEvent *event) override;

    // Initialisation (appelées par le constructeur)
    void applyStyleSheet();
    void setupProjectGridConnections();
    void setupPipelineConnections();
    void setupHomePageConnections();
    void setupMenuConnections();
    void setupPageConnections();
    void setupPairAlignmentConnections();

    void selectPair(int pairIndex);
    void selectNextDubiousPair();
    void validatePair(int pairIndex);
    void applyManualPairOffset(int pairIndex, double dx, double dy);
    void applyManualPairPoints(int pairIndex, const QVector<QPointF> &pointsInA, const QVector<QPointF> &pointsInB);
    void submitManualCorrection(const QString &pairLabel, const FrontendNs::ManualCorrection &correction);
    void refreshPairViews();
    // Remplace les paires et les lignes de couture affichées par celles du dernier calcul (vides : tout est effacé).
    void applyPipelineResults(const PipelineResults &results);

    void refreshGrid();
    void refreshGridControls();
    void chooseTilesDirectory();
    bool chooseReferenceImage();
    void showReferenceComparison(const QString &referencePath, const QString &tilePath,
                                 const QImage &referenceImage, const QImage &tileImage);
    void setMode(int mode);
    void refreshProjectInfo();
    void invalidatePipelineResults();
    void saveProject();
    bool saveProjectToFile();
    bool confirmDiscardUnsavedChanges();
    void markProjectModified();
    void refreshWindowTitle();
    bool projectModified = false;
    bool switchingProject = false;
    bool pipelineResultsStale = false;
    // Construit et valide le script JSON décrivant la suite des étapes backend.
    QString generatePipelineScript(int firstStep) const;
    void requestRun(int firstStep);
    void showExportDialog();
    void updateStatus(const QString &message);
    void refreshProgressVisibility();

private slots:
    void initializeNewProject(const QString &name, const QString &tileDirectory,
                              const QString &selectedOutputDirectory,
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
    PipelineRunnerClass *pipelineRunner = nullptr;
    WorkspaceUI *workspaceUI = nullptr;
    MenuBarManager *menuBarManager = nullptr;
    QString projectName = QStringLiteral("Polyptik");
    QString projectPath;
    QString outputDirectory;
    bool projectCreated = false;
    int currentMode = 4;
    int zoomPercent = 100;
    bool sideBarVisible = true;
    bool logVisible = false;
    bool maskVisible = true;
    QVector<FrontendUtils::PairAlignmentInfo> m_pairs;
    int m_currentPairIndex = 0;
    bool isClosing = false;
    FrontendNs::ProjectSetup buildCurrentProjectSetup() const;
};
