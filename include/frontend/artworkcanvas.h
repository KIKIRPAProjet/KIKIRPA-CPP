#ifndef ARTWORKCANVAS_H
#define ARTWORKCANVAS_H

#include "projectgrid.h"
#include "frontendutils.h"

#include <QRect>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QSize>

#include <functional>

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QTimer;
template <typename T>
class QFutureWatcher;
class QWheelEvent;

/**
 * Zone d'affichage centrale : œuvre, grille de tuiles, paire, masque et couture.
 * L'implémentation est répartie entre artworkcanvas.cpp (événements, paintEvent)
 * et artworkcanvas_draw.cpp (fonctions de dessin).
 */
class ArtworkCanvas final : public QWidget
{
public:
    explicit ArtworkCanvas(QWidget *parent = nullptr);
    ~ArtworkCanvas() override;

    void setMode(int mode) { modeValue = mode; update(); }
    void setZoom(double zoom);
    double zoomPercentValue() const { return zoomPercent; }
    void fitMosaicToView();
    void zoomIn();
    void zoomOut();
    void clearMosaic();
    bool setMosaicTiff(const QString &path, QString *errorMessage = nullptr);
    void setZoomChangedCallback(std::function<void(double)> callback);
    void setImagePaths(const QStringList &paths) { imagePaths = paths; update(); }
    void setGridState(int rows, int columns, int order, const QVector<GridTile> &tiles,
                      int selectedCell);
    void setGridCallbacks(std::function<void(int)> selectionChanged,
                          std::function<void(int, int)> cellsSwapped);
    void toggleComparison() { comparison = !comparison; update(); }
    void setComparisonVisible(bool visible) { comparison = visible; update(); }
    void toggleMask() { maskVisible = !maskVisible; update(); }
    void setMaskVisible(bool visible) { maskVisible = visible; update(); }
    void setMosaicPreview(const QImage &image);
    void setSeamVisible(bool visible); //afficher ou masquer la ligne de couture dans les modes Assemblage et Export 
    void setAlignmentErrorVisible(bool visible); //afficher ou masquer l'erreur d'alignement par paire
    void setPairOverlayMode(bool enabled); //superposer les deux tuiles (différence) en vue Paire
    void setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *) override;

private:
    struct MosaicRegionResult {
        QImage image;
        QRectF fullResolutionRegion;
        int level = 0;
        QString errorMessage;
    };

    // artworkcanvas_draw.cpp
    void drawGrid(QPainter &p);
    void drawPainting(QPainter &p, const QRect &r);
    void drawPhoto(QPainter &p, const QRect &r, const QString &path,
                   int rotation = 0, bool flipped = false);

    int gridCellAt(const QPoint &point) const;
    QRectF mosaicImageRect() const;
    QPointF clampedMosaicOrigin(const QPointF &origin, double scale) const;
    void applyMosaicZoom(double zoomPercent, const QPointF &anchor);
    void requestMosaicRegion();
    void startMosaicRegionRead();
    void acceptMosaicRegion(quint64 requestId, const MosaicRegionResult &result);
    void updateZoom(double zoom, bool notify);

    int modeValue = 4;
    double zoomPercent = 12.0;
    double mosaicScale = 0.12;
    QStringList imagePaths;
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
    QString mosaicTiffPath;
    QSize mosaicSize;
    QPointF mosaicOrigin;
    QImage mosaicRegionImage;
    QRectF mosaicRegionInFullResolution;
    int mosaicRegionLevel = -1;
    QString mosaicReadError;
    quint64 mosaicRequestId = 0;
    QTimer *mosaicRequestTimer = nullptr;
    QFutureWatcher<MosaicRegionResult> *activeMosaicRead = nullptr;
    bool mosaicReadPending = false;
    QPoint lastPanPosition;
    bool panningMosaic = false;
    std::function<void(double)> zoomChangedCallback;
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    FrontendUtils::PairAlignmentInfo currentPair;
};

#endif // ARTWORKCANVAS_H
