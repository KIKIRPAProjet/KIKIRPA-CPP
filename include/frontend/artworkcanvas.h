#pragma once

#include "projectgrid.h"
#include "frontendutils.h"
#include "pipeline_results.h"

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
    // Calque des lignes de couture calculées (même taille que l'aperçu), affiché en modes Assemblage et Ligne de couture.
    void setSeamOverlay(const QImage &overlay) { m_seamOverlay = overlay; update(); }
    // Toutes les tuiles et toutes les paires du dernier calcul, pour la vue d'ensemble du mode Alignement.
    void setAlignmentOverview(const QVector<ResultTile> &tiles, const QVector<FrontendUtils::PairAlignmentInfo> &pairs)
    {
        m_resultTiles = tiles;
        m_allPairs = pairs;
        update();
    }
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
    // Aperçu de la mosaïque ajusté à la vue, avec ou sans le calque des coutures ; false s'il n'y a pas d'aperçu.
    bool drawMosaicPreview(QPainter &p, bool withSeams);
    // Mode Alignement avec les données du backend : toutes les tuiles à leur case et les points de toutes les paires,
    // ou, en mode superposé, la paire sélectionnée en grand.
    void drawPairView(QPainter &p);
    void drawTilesOverview(QPainter &p);
    void drawSuperposedPair(QPainter &p);
    void drawCenteredMessage(QPainter &p, const QString &message);

    int gridCellAt(const QPoint &point) const;

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
    QImage m_seamOverlay;
    QVector<ResultTile> m_resultTiles;
    QVector<FrontendUtils::PairAlignmentInfo> m_allPairs;
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    FrontendUtils::PairAlignmentInfo currentPair;
};
