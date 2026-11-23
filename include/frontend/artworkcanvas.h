#pragma once

#include "projectgrid.h"
#include "frontendutils.h"

#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QImage>

#include <cstdint>
#include <functional>

class QKeyEvent;
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
    void setGridState(int rows, int columns, int order, const QVector<GridTile> &tiles,
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

    void setSeamVisible(bool visible); //afficher ou masquer la ligne de couture dans les modes Assemblage et Export 
    void setAlignmentErrorVisible(bool visible); //afficher ou masquer l'erreur d'alignement par paire
    void setPairOverlayMode(bool enabled); //superposer les deux tuiles (différence) en vue Paire
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

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *) override;

private:
    // artworkcanvas_draw.cpp
    void drawGrid(QPainter &p);
    void drawPainting(QPainter &p, const QRect &r);
    void drawPhoto(QPainter &p, const QRect &r, const QString &path,
                   int rotation = 0, bool flipped = false);

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
    int zoomPercent = 12;
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
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    FrontendUtils::PairAlignmentInfo currentPair;

    QRect mosaicDisplayedRect() const;
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
