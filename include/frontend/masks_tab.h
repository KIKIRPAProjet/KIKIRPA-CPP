#pragma once

#include <QWidget>

#include "image_layer.h"
#include <QVector>
class ArtworkCanvas;
class MainWindow;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QSlider;
class MasksTab : public QWidget
{
    Q_OBJECT
public:
    explicit MasksTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);
    void setImageLayers(const QVector<ImageLayer> &layers);

private:
    void syncSelectedLayerControls();

    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;
    QGroupBox *imageLayerControls = nullptr;
    QSlider *globalOpacitySlider = nullptr;
    QComboBox *layerSelector = nullptr;
    QSlider *layerOpacitySlider = nullptr;
    QCheckBox *layerVisibilityCheck = nullptr;
};
