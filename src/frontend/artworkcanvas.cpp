#include "artworkcanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace {
constexpr int kMinimumWidth = 360;
constexpr int kMinimumHeight = 120;   // assez bas pour laisser grandir le panneau du bas ; le dessin est recadré
constexpr int kPairGutter = 22;
constexpr int kMinImageWidth = 130;
constexpr int kMinImageHeight = 180;
constexpr double kPairAspect = 0.78;
constexpr double kSingleAspect = 0.70;

// Modes de l'espace de travail (voir FrontendUtils::MODE_NAMES).
constexpr int kModeGrid = 1;
constexpr int kModeMasks = 2;
constexpr int kModePair = 3;
constexpr int MODE_SEAM = 4;
constexpr int kModeAssembly = 5;
constexpr int kModeExport = 6;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
}

//enregistrement de la nouvelle paire choisie + envoie l'update pour afficher les bonnes paires à l'écran
void ArtworkCanvas::setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair)
{
    currentPair = pair;
    update();
}

void ArtworkCanvas::setGridState(int rows, int columns, int order,
                                 const QVector<GridTile> &tiles, int selectedCell)
{
    gridRows = rows;
    gridColumns = columns;
    captureOrder = order;
    gridTiles = tiles;
    selectedGridCell = selectedCell;
    update();
}

void ArtworkCanvas::setGridCallbacks(std::function<void(int)> selectionChanged,
                                     std::function<void(int, int)> cellsSwapped)
{
    selectionChangedCallback = std::move(selectionChanged);
    cellsSwappedCallback = std::move(cellsSwapped);
}

void ArtworkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (modeValue == kModeGrid && event->button() == Qt::LeftButton) {
        dragStartCell = gridCellAt(event->pos());
        if (dragStartCell >= 0) {
            selectedGridCell = dragStartCell;
            if (selectionChangedCallback)
                selectionChangedCallback(selectedGridCell);
            update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ArtworkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (modeValue == kModeGrid && event->button() == Qt::LeftButton) {
        const int targetCell = gridCellAt(event->pos());
        if (dragStartCell >= 0 && targetCell >= 0 && targetCell != dragStartCell) {
            if (cellsSwappedCallback)
                cellsSwappedCallback(dragStartCell, targetCell);
        }
        dragStartCell = -1;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

int ArtworkCanvas::gridCellAt(const QPoint &point) const
{
    for (int index = 0; index < gridCellRects.size(); ++index)
        if (gridCellRects.at(index).contains(point))
            return index;
    return -1;
}

// Dessine la zone de recouvrement (orange) et les points retenus (verts) / rejetés (rouges) de la paire courante.
// exemple avant la connection au backend : les positions (x, y) des points sont simulées en attendant la connexion avec Matches::pointsInA / pointsInB du backend.
void ArtworkCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#484848"));

    // Export : la mosaïque seule. Assemblage : la mosaïque et, si le calque est actif, ses coutures.
    if ((modeValue == kModeExport && drawMosaicPreview(p, false)) ||
        (modeValue == kModeAssembly && drawMosaicPreview(p, seamVisible)))
        return;
    if (modeValue == MODE_SEAM) {
        if (!drawMosaicPreview(p, true))
            drawCenteredMessage(p, tr("Lancez l'assemblage pour afficher les lignes de couture calculées."));
        return;
    }

    if (modeValue == kModeGrid) {
        drawGrid(p);
        return;
    }

    const bool pair = modeValue == kModePair;
    const int gutter = pair ? kPairGutter : 0;
    const int maxW = pair ? (width() - 90) / 2 : width() - 130;
    const int maxH = height() - 80;
    const double aspect = pair ? kPairAspect : kSingleAspect;
    int imageH = qMin(maxH, int(maxW / aspect));
    int imageW = int(imageH * aspect);
    if (imageW > maxW) {
        imageW = maxW;
        imageH = int(imageW / aspect);
    }
    imageW = qMax(kMinImageWidth, imageW);
    imageH = qMax(kMinImageHeight, imageH);
    const int totalW = pair ? imageW * 2 + gutter : imageW;
    const int x0 = (width() - totalW) / 2;
    const int y0 = (height() - imageH) / 2;

    const QRect firstImageRect(x0, y0, imageW, imageH);
    if (pair) {
        drawPairView(p);
        return;
    }

    if (!imagePaths.isEmpty())
        drawPhoto(p, firstImageRect, imagePaths.first());
    else
        drawPainting(p, firstImageRect);

    if (modeValue == kModeMasks && maskVisible) {
        QPainterPath maskPath;
        maskPath.moveTo(x0 + imageW * 0.10, y0);
        maskPath.lineTo(x0 + imageW * 0.98, y0);
        maskPath.lineTo(x0 + imageW * 0.98, y0 + imageH * 0.06);
        maskPath.lineTo(x0 + imageW * 0.12, y0 + imageH * 0.10);
        maskPath.closeSubpath();
        p.fillPath(maskPath, QColor(185, 39, 36, 150));
        p.setPen(QPen(QColor("#ef5149"), 2));
        p.drawPath(maskPath);
        p.setPen(QColor("#eeeeee"));
        p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft,
                   tr("Masque d'exclusion : proposition"));
    } else if (modeValue == kModeAssembly || modeValue == kModeExport) {
        // Pas encore de mosaïque calculée : la première tuile, sans couture ni erreur simulées.
        if (comparison) {
            p.setPen(QPen(QColor("#f0cf36"), 2));
            p.drawLine(x0 + imageW / 2, y0, x0 + imageW / 2, y0 + imageH);
            p.fillRect(QRect(x0, y0, imageW / 2, imageH), QColor(225, 188, 40, 30));
        }
        p.setPen(QPen(QColor("#e4ca37"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(x0, y0, imageW, imageH));
        p.setPen(QColor("#dddddd"));
        p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft,
                   tr("La mosaïque s'affichera ici après l'assemblage."));
    }

    p.setPen(QColor("#dddddd"));
    p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
               QString::number(zoomPercent) + QStringLiteral(" %"));
}

void ArtworkCanvas::setSeamVisible(bool visible)
{
    if(seamVisible != visible){
        seamVisible = visible;
        update();
    }
}

void ArtworkCanvas::setAlignmentErrorVisible(bool visible)
{
    if (alignmentErrorVisible != visible) {
        alignmentErrorVisible = visible;
        update();
    }
}

void ArtworkCanvas::setPairOverlayMode(bool enabled)
{
    if (pairOverlayMode != enabled) {
        pairOverlayMode = enabled;
        update();
    }
}
