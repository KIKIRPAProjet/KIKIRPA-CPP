#pragma once

#include "projectgrid.h"
#include "image_layer.h"
#include "frontendutils.h"

#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QImage>

#include <functional>


class QMouseEvent;
class QPaintEvent;
class QPainter;

/**
 * Zone d'affichage centrale : œuvre, grille de tuiles, paire, masque et couture.
 * L'implémentation est répartie entre artworkcanvas.cpp (événements, paintEvent)
 * et artworkcanvas_draw.cpp (fonctions de dessin).
 */
class ArtworkCanvas final : public QWidget
{
public:
    explicit ArtworkCanvas(QWidget *parent = nullptr);

    void setMode(int mode) { modeValue = mode; update(); }
    void setZoom(int zoom) { zoomPercent = zoom; update(); }
    void setImagePaths(const QStringList &paths) { imagePaths = paths; update(); }
    void setImageLayers(const QVector<ImageLayer> &layers)
    {
        imageLayers = layers;
        globalImageLayerOpacity = 1.0;
        update();
    }

    // Facteur de rendu commun aux couches TIFF en mode Assemblage.
    void setGlobalImageLayerOpacity(qreal opacity)
    {
        globalImageLayerOpacity = qBound(qreal(0), opacity, qreal(1));
        update();
    }

    // Renvoie les réglages courants de la couche ou nullptr si elle n'existe plus.
    const ImageLayer *findImageLayer(const QString &id) const
    {
        for (const ImageLayer &layer : imageLayers) {
            if (layer.id == id)
                return &layer;
        }
        return nullptr;
    }

    void setImageLayerOpacity(const QString &id, qreal opacity)
    {
        for (ImageLayer &layer : imageLayers) {
            if (layer.id == id) {
                layer.opacity = qBound(qreal(0), opacity, qreal(1));
                update();
                return;
            }
        }
    }

    void setImageLayerVisible(const QString &id, bool visible)
    {
        for (ImageLayer &layer : imageLayers) {
            if (layer.id == id) {
                layer.visible = visible;
                update();
                return;
            }
        }
    }

    void setGridState(int rows, int columns, int order, const QVector<GridTile> &tiles,
                      int selectedCell);
    void setGridCallbacks(std::function<void(int)> selectionChanged,
                          std::function<void(int, int)> cellsSwapped);
    void toggleComparison() { comparison = !comparison; update(); }
    void setComparisonVisible(bool visible) { comparison = visible; update(); }
    void toggleMask() { maskVisible = !maskVisible; update(); }
    void setMaskVisible(bool visible) { maskVisible = visible; update(); }
    void setMosaicPreview(const QImage &image) {
        m_mosaicPreview = image;
        update(); // Demande à Qt de redessiner le canevas immédiatement
    }
    const QImage &mosaicPreview() const { return m_mosaicPreview; }
    void setSeamVisible(bool visible); //afficher ou masquer la ligne de couture dans les modes Assemblage et Export 
    void setAlignmentErrorVisible(bool visible); //afficher ou masquer l'erreur d'alignement par paire
    void setPairOverlayMode(bool enabled); //superposer les deux tuiles (différence) en vue Paire
    void setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *) override;

private:
    // artworkcanvas_draw.cpp
    void drawGrid(QPainter &p);
    void drawPainting(QPainter &p, const QRect &r);
    void drawPhoto(QPainter &p, const QRect &r, const QString &path,
                   int rotation = 0, bool flipped = false);
    bool drawImageLayers(QPainter &p, const QRect &area);
    int gridCellAt(const QPoint &point) const;

    int modeValue = 4;
    int zoomPercent = 12;
    QStringList imagePaths;
    QVector<ImageLayer> imageLayers;
    qreal globalImageLayerOpacity = 1.0;
    QVector<GridTile> gridTiles;
    QVector<QRect> gridCellRects;
    std::function<void(int)> selectionChangedCallback;
    std::function<void(int, int)> cellsSwappedCallback;
    int gridRows = 3;
    int gridColumns = 2;
    int captureOrder = 0;
    int selectedGridCell = 0;
    int dragStartCell = -1;
    bool maskVisible = true;
    bool comparison = false;
    QImage m_mosaicPreview;
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    FrontendUtils::PairAlignmentInfo currentPair;
};
