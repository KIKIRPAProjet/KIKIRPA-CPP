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
class QVBoxLayout;

class GridTab : public QScrollArea
{
    Q_OBJECT

public:
    explicit GridTab(MainWindow *mainWindow, ProjectGrid *projectGrid, ArtworkCanvas *canvas, QWidget *parent = nullptr);

    void updateState(const ProjectGrid::State &gridState);
    QComboBox *captureOrderCombo() const { return m_captureOrderCombo; }

private:
    // sous-fonctions de création
    QWidget *createGridDimensionsGroup(QWidget *parent);
    QWidget *createGridCaptureGroup(QWidget *parent);
    void createTileActions(QVBoxLayout *layout);
    void createReferenceActions(QVBoxLayout *layout);
    void setupConnections();

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
    QComboBox *m_captureOrderCombo = nullptr;
    QLabel *m_selectedGridTileLabel = nullptr;
    QPushButton *m_rotateTileButton = nullptr;
    QPushButton *m_flipTileButton = nullptr;
    QPushButton *m_emptyCellButton = nullptr;
    QSpinBox *m_horizontalOverlapSpin = nullptr;
    QSpinBox *m_verticalOverlapSpin = nullptr;
    QCheckBox *m_linkedOverlapCheck = nullptr;
    QComboBox *m_modalityCombo = nullptr;
    QDoubleSpinBox *m_scaleSpin = nullptr;
    QCheckBox *m_automaticUncertaintyCheck = nullptr;
    QDoubleSpinBox *m_uncertaintySpin = nullptr;
    QLabel *m_referencePathLabel = nullptr;
    QPushButton *m_importReferenceButton = nullptr;
    QPushButton *m_compareReferenceButton = nullptr;
    QPushButton *m_validateGridButton = nullptr;
};

#endif // GRID_TAB_H
