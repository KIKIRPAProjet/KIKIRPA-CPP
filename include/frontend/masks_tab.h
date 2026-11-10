#ifndef MASKS_TAB_H
#define MASKS_TAB_H

#include <QWidget>

#include "image_layer.h"
#include <QVector>
class MainWindow;
class ArtworkCanvas;
class QVBoxLayout;
class MasksTab : public QWidget
{
    Q_OBJECT
public:
    explicit MasksTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);
    void setImageLayers(const QVector<ImageLayer> &layers);

private:
    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;
    QVBoxLayout *m_imageLayersLayout = nullptr;
};

#endif // MASKS_TAB_H