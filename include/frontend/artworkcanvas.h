#pragma once

#include "projectgrid.h"
#include "image_layer.h"
#include "frontendutils.h"
#include "pipeline_results.h"

#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QImage>
#include <QCache>
#include <QPoint>
#include <QPointF>
#include <QTransform>

#include <cstdint>
#include <functional>
#include <optional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;

/**
 * Zone d'affichage centrale : œuvre, grille de tuiles, paire, masque et couture.
 * L'implémentation est répartie entre artworkcanvas.cpp (événements, paintEvent)
 * et artworkcanvas_draw.cpp (fonctions de dessin).
 */
class ArtworkCanvas final : public QWidget
{
    Q_OBJECT

public:
    explicit ArtworkCanvas(QWidget *parent = nullptr);

    void setMode(int mode) { modeValue = mode; update(); }
    void setZoom(int zoom) { zoomPercent = zoom; update(); }
    void setImageLayers(const QVector<ImageLayer> &layers, const QRectF &bounds = {})
    {
        imageLayers = layers;
        imageLayerBounds = bounds;
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

    void setFusedMosaicVisible(bool visible) { fusedMosaicVisible = visible; update(); }
    void setGridState(int rows, int columns, const QVector<GridTile>& tiles,
        int selectedCell);
    void setGridCallbacks(std::function<void(int)> selectionChanged,
                          std::function<void(int, int)> cellsSwapped);
    void toggleComparison() { comparison = !comparison; update(); }
    void setComparisonVisible(bool visible) { comparison = visible; update(); }
    void toggleMask() { maskVisible = !maskVisible; update(); }
    void setMaskVisible(bool visible) { maskVisible = visible; update(); }
    // Définit et mémorise l'aperçu de la mosaïque assemblée pour le mode Image finale.
    void setMosaicPreview(const QImage &image);

    // Retourne l'aperçu actuel de la mosaïque (recadré ou original).
    const QImage &mosaicPreview() const { return m_mosaicPreview; }
    // Calque des lignes de couture calculées (même taille que l'aperçu), affiché en modes Assemblage et Ligne de couture.
    void setSeamLines(const SeamLines &seams) { m_seamLines = seams; update(); }
    // Toutes les tuiles et toutes les paires du dernier calcul, pour la vue d'ensemble du mode Alignement.
    void setAlignmentOverview(const QVector<ResultTile> &tiles, const QVector<FrontendUtils::PairAlignmentInfo> &pairs,
                              bool preserveViewState = false)
    {
        if (!preserveViewState) {
            stopPointPicking();
            setPairOverlayMode(false);
            setPairIsolated(false);
        }
        m_resultTiles = tiles;
        m_allPairs = pairs;
        update();
    }
    void setSeamVisible(bool visible); //afficher ou masquer la ligne de couture dans les modes Assemblage et Export
    void setAlignmentErrorVisible(bool visible); //afficher ou masquer l'erreur d'alignement par paire
    void setPairOverlayMode(bool enabled); //superposer les deux tuiles (différence) en vue Paire
    void setPairIsolated(bool isolated);   // afficher uniquement les 2 tuiles de la paire courante
    bool isPairIsolated() const { return m_pairIsolated; }
    bool isPairOverlayMode() const { return pairOverlayMode; }
    void startPointPicking(const QVector<QPointF> &initialPointsA = {}, const QVector<QPointF> &initialPointsB = {});
    void undoLastPickedPoint();
    void stopPointPicking();
    bool isPickingPoints() const { return m_pickingPoints; }
    const QVector<QPointF> &pickedPointsInA() const { return m_manualPointsA; }
    const QVector<QPointF> &pickedPointsInB() const { return m_manualPointsB; }
    bool hasPendingPointInA() const { return m_pendingPointInA.has_value(); }
    void setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair);

    // Active un outil de dessin ou d'édition (par exemple l'outil de recadrage).
    void setActiveTool(int tool) { m_activeTool = tool; update(); }

    // Retourne l'outil actuellement sélectionné (-1 si aucun).
    int activeTool() const { return m_activeTool; }

    // Indique si un recadrage a été appliqué sur la mosaïque affichée.
    bool isCropped() const { return m_isCropped; }

    // Indique si une zone de sélection de recadrage valide est activement tracée.
    bool hasActiveCropSelection() const {
        return m_cropSelectionRect.isValid() && m_cropSelectionRect.width() >= 15 && m_cropSelectionRect.height() >= 15;
    }

    // Retourne le rectangle de recadrage normalisé entre 0.0 et 1.0 par rapport à l'image pleine résolution.
    QRectF cropNormalizedRect() const { return m_cropNormalizedRect; }

    // Valide et applique la sélection de recadrage actuelle sur l'image affichée.
    void applyCrop();

    // Annule la boîte de sélection en cours sans modifier le recadrage déjà appliqué.
    void cancelCropSelection();

    // Rétablit l'image originale non recadrée.
    void resetCrop();

    void resetView();
    void setImagePaths(const QStringList &paths) { imagePaths = paths; m_imageCache.clear(); update(); }

    signals:
        void zoomChanged(int zoomPercent);
        void importTilesRequested();
        // Clic (sans glisser) sur un lien, une zone de recouvrement ou une jonction de la vue d'ensemble de l'alignement.
        void pairClicked(int pairIndex);
        void pairViewModeChanged();
        void manualPointsChanged();
        void pointPickingCancelled();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // artworkcanvas_draw.cpp
    void drawEmptyState(QPainter &p);
    void drawGrid(QPainter &p);
    void drawPainting(QPainter &p, const QRect &r);
    void drawPhoto(QPainter &p, const QRect &r, const QString &path,
                   int rotation = 0, bool flipped = false);
    bool drawAssemblyLayers(QPainter &p);
    // Aperçu de la mosaïque ajusté à la vue, avec ou sans le calque des coutures ; false s'il n'y a pas d'aperçu.
    bool drawMosaicPreview(QPainter &p, bool withSeams);
    // Mode Alignement avec les données du backend : toutes les tuiles à leur case et les points de toutes les paires,
    // ou, en mode superposé, la paire sélectionnée en grand.
    void drawPairView(QPainter &p);
    void drawTilesOverview(QPainter &p);
    void drawFocusedPair(QPainter &p);
    void drawSuperposedPair(QPainter &p);
    void drawManualPickingOverlay(QPainter &p, const QRectF &rectA, double scaleA,
                                  const QRectF &rectB, double scaleB);
    bool handlePointPickingClick(const QPointF &scenePos);
    void drawCenteredMessage(QPainter &p, const QString &message);

    bool hasActiveImages() const;
    QRect emptyImportCardRect() const;
    int gridCellAt(const QPoint &point) const;
    void drawCropOverlay(QPainter &painter, const QRect &imageRect);

    enum class CropHandle : std::uint8_t {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Top,
        Bottom,
        Left,
        Right,
        Move
    };

    CropHandle hitTestCropHandle(const QPoint &pos) const;

    int modeValue = 4;
    int zoomPercent = 100;
    QStringList imagePaths;
    QVector<ImageLayer> imageLayers;
    QRectF imageLayerBounds;
    qreal globalImageLayerOpacity = 1.0;
    bool fusedMosaicVisible = false;
    QVector<GridTile> gridTiles;
    QVector<QRect> gridCellRects;
    std::function<void(int)> selectionChangedCallback;
    std::function<void(int, int)> cellsSwappedCallback;
    int gridRows = 3;
    int gridColumns = 2;
    int selectedGridCell = 0;
    int dragStartCell = -1;
    int dragTargetCell = -1;
    bool maskVisible = true;
    bool comparison = false;
    QImage m_mosaicPreview;
    SeamLines m_seamLines;
    QVector<ResultTile> m_resultTiles;
    QVector<FrontendUtils::PairAlignmentInfo> m_allPairs;
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    bool m_pairIsolated = false;
    bool m_pickingPoints = false;
    std::optional<QPointF> m_pendingPointInA;
    QPointF m_hoverScenePos;
    QVector<QPointF> m_manualPointsA;
    QVector<QPointF> m_manualPointsB;
    FrontendUtils::PairAlignmentInfo currentPair;

    QPointF m_panOffset;
    QPoint m_lastPanPos;
    bool m_panning = false;
    bool m_emptyCardHovered = false;
    QCache<QString, QImage> m_imageCache;

    QRect mosaicDisplayedRect() const;
    // Zoom et déplacement de la vue : repère de l'image non zoomée -> écran (paintEvent, recadrage).
    QTransform viewTransform() const;
    // Paire de la vue d'ensemble sous ce point du repère non zoomé, -1 si aucune (artworkcanvas_draw.cpp).
    int pairAt(const QPointF &scenePos) const;
    bool isPairOverviewShown() const;
    QPoint m_pressPos;   // pour distinguer un clic d'un glisser (déplacement de la vue)
    int m_activeTool = -1;
    QImage m_originalMosaicPreview;
    bool m_isCropping = false;
    bool m_isCropped = false;
    QRect m_cropSelectionRect;
    QRectF m_cropNormalizedRect = QRectF(0, 0, 1, 1);
    QPoint m_cropStartPos;
    CropHandle m_activeHandle = CropHandle::None;
    QPoint m_cropDragStart;
    QRect m_cropRectAtDragStart;
};
