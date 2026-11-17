// include/frontend/mainwindow.h
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QKeyEvent;
class QWidget;
class ProjectGrid;
class SimulationRunner;
class WorkspaceUI;
class MenuBarManager;
class QImage;

// Coordonne les composants de l'interface et les actions du projet ouvert.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Construit la fenêtre principale et initialise ses composants.
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

public slots:
    // Met à jour l'étape affichée et la progression.
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

    void keyPressEvent(QKeyEvent *event) override;

    // Applique le style visuel de l'application.
    void applyStyleSheet();

    // Relie chaque composant aux événements de la grille, des menus et du pipeline.
    void setupProjectGridConnections();
    void setupSimulationConnections();
    void setupHomePageConnections();
    void setupMenuConnections();
    void setupPageConnections();

    // Met à jour la grille à partir de l'état du projet.
    void refreshGrid();

    // Met à jour les contrôles associés à la grille.
    void refreshGridControls();

    // Ouvre un dialogue pour choisir les tuiles ou l'image de référence.
    void chooseTilesDirectory();
    bool chooseReferenceImage();

    // Affiche une comparaison visuelle entre l'image de référence et une tuile.
    void showReferenceComparison(const QString &referencePath, const QString &tilePath,
                                 const QImage &referenceImage, const QImage &tileImage);

    // Change le mode d'affichage.
    void setMode(int mode);

    // Actualise les informations du projet et l'enregistre.
    void refreshProjectInfo();
    void saveProject();

    // Écrit le script backend du projet et renvoie son chemin.
    QString writeBackendScript() const;

    // Lance le pipeline ou ouvre le dialogue d'export du rapport.
    void requestRun(int firstStep);
    void showExportDialog();

    // Met à jour les informations d'état et de progression de l'interface.
    void updateStatus(const QString &message);
    void refreshProgressVisibility();

private slots:
    // Crée un projet et applique les chemins et tuiles choisis.
    void initializeNewProject(const QString &name, const QString &tileDirectory,
                              const QString &selectedOutputDirectory,
                              const QStringList &tilePaths);

    // Charge un projet existant.
    void applyLoadedProject(const QString &path);

    // Ouvre depuis l'accueil le projet actuellement sélectionné.
    void openCurrentProjectFromHome();

    // Traite l'import des tuiles.
    void handleTilesImported(const QString &directory, int importedCount);

    // Affiche les messages de validation de la grille et les erreurs associées.
    void showGridMessage(const QString &title, const QString &message, bool warning);
    void handleGridValidation(bool valid, int activeTileCount, const QStringList &errors);

    // Affiche les outils de comparaison.
    void showComparisonDialog();

    // Affiche ou retire la commande d'annulation d'exécution.
    void showCancelRunButton();
    void removeCancelRunButton();

private:
    // Composants d'interface coordonnés par cette fenêtre.
    ProjectGrid *projectGrid = nullptr;
    SimulationRunner *simulationRunner = nullptr;
    WorkspaceUI *workspaceUI = nullptr;
    MenuBarManager *menuBarManager = nullptr;

    // Informations et chemins du projet ouvert.
    QString projectName = QStringLiteral("VanDerVeken");
    QString projectPath;
    QString outputDirectory;
    QString backendScriptPath;

    // État de la présentation courante.
    bool projectCreated = false;
    int currentMode = 4;
    int zoomPercent = 12;
    bool panelsVisible = true;
    bool maskVisible = true;
};
