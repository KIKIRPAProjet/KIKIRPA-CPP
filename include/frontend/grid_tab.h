#pragma once

#include <QScrollArea>
#include "projectgrid.h"

class MainWindow;
class ArtworkCanvas;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

class GridTab : public QScrollArea
{
    Q_OBJECT

public:
    explicit GridTab(MainWindow *mainWindow, ProjectGrid *projectGrid, ArtworkCanvas *canvas, QWidget *parent = nullptr);

    void updateState(const ProjectGrid::State &gridState);


private:
    // sous-fonctions de création
    QWidget *createGridDimensionsGroup(QWidget *parent);
    QWidget *createGridCaptureGroup(QWidget *parent);
    void createTileActions(QVBoxLayout *layout);
    void createReferenceActions(QVBoxLayout *layout);
    void createCacheActions(QVBoxLayout *layout);
    void setupConnections();
    // Demande confirmation, en annonçant la place récupérée, puis vide le cache des pyramides du projet.
    void freePyramidCache();
    // Vrai, et le dit à l'utilisateur, si un calcul tourne : il lit les fichiers qu'on allait supprimer.
    bool warnIfPipelineRunning();
    void updateCacheButtonState();

    // Les sous-fonctions de mise à jour
    void updateParameters(const ProjectGrid::State &gridState);
    void updateSummary(const ProjectGrid::State &gridState);
    void updateSelectionLabel(const ProjectGrid::State &gridState);
    void updateReferenceLabel(const ProjectGrid::State &gridState);

    // Les accès aux autres modules
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    ArtworkCanvas *m_canvas;

    QWidget *m_gridTabPage = nullptr;
    QLabel *m_gridSummaryLabel = nullptr;
    QSpinBox *m_gridRowsSpin = nullptr;
    QSpinBox *m_gridColumnsSpin = nullptr;
    QLabel *m_selectedGridTileLabel = nullptr;
    QPushButton *m_rotateTileButton = nullptr;
    QPushButton *m_flipTileButton = nullptr;
    QPushButton *m_emptyCellButton = nullptr;
    QSpinBox *m_horizontalOverlapSpin = nullptr;
    QSpinBox *m_verticalOverlapSpin = nullptr;
    QCheckBox *m_linkedOverlapCheck = nullptr;
    QCheckBox *m_automaticUncertaintyCheck = nullptr;
    QDoubleSpinBox *m_uncertaintySpin = nullptr;
    QLabel *m_referencePathLabel = nullptr;
    QPushButton *m_importReferenceButton = nullptr;
    QPushButton *m_compareReferenceButton = nullptr;
    QPushButton *m_validateGridButton = nullptr;
    QPushButton *m_freeCacheButton = nullptr;
};
