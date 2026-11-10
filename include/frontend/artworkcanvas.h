#ifndef ARTWORKCANVAS_H
#define ARTWORKCANVAS_H

#include "projectgrid.h"
#include "frontendutils.h"

#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>

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
    bool seamVisible = true;
    bool alignmentErrorVisible = true;
    bool pairOverlayMode = false;
    FrontendUtils::PairAlignmentInfo currentPair;
};

#endif // ARTWORKCANVAS_H
