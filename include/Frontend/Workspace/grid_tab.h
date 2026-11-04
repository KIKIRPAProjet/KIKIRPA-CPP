#ifndef GRID_TAB_H
#define GRID_TAB_H

#include <QScrollArea>
#include "projectgrid.h"

class MainWindow;
class ArtworkCanvas;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;

public:
    explicit GridTab(MainWindow *mainWindow, ProjectGrid *projectGrid, ArtworkCanvas *canvas, QWidget *parent = nullptr);

    void updateState(const ProjectGrid::State &gridState);

private:
    // sous-fonctions de création
    QWidget *createDimensionsGroup();
    QWidget *createCaptureGroup();
    void setupConnections(QPushButton *rotateTile, QPushButton *flipTile, 
                          QPushButton *importReference, QPushButton *compareReference, 
                          QPushButton *validateGridButton);

    // Les sous-fonctions de mise à jour
    void updateParameters(const ProjectGrid::State &gridState);
    void updateSummary(const ProjectGrid::State &gridState);

    // Les accès aux autres modules
    MainWindow *m_mainWindow;
    ProjectGrid *m_projectGrid;
    ArtworkCanvas *m_canvas;

    QWidget *m_gridTabPage = nullptr;
    QLabel *m_gridSummaryLabel = nullptr;
    QSpinBox *m_gridRowsSpin = nullptr;
    QSpinBox *m_gridColumnsSpin = nullptr;
    QComboBox *m_captureOrderCombo = nullptr;
    QLabel *m_selectedGridTileLabel = nullptr;
    QPushButton *m_emptyCellButton = nullptr;
    QSpinBox *m_horizontalOverlapSpin = nullptr;
    QSpinBox *m_verticalOverlapSpin = nullptr;
    QCheckBox *m_linkedOverlapCheck = nullptr;
    QComboBox *m_modalityCombo = nullptr;
    QDoubleSpinBox *m_scaleSpin = nullptr;
    QCheckBox *m_automaticUncertaintyCheck = nullptr;
    QDoubleSpinBox *m_uncertaintySpin = nullptr;
    QLabel *m_referencePathLabel = nullptr;
};

#endif 