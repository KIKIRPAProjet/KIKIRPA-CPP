#include "artworkcanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace {
constexpr int kMinimumWidth = 360;
constexpr int kMinimumHeight = 260;
constexpr int kPairGutter = 22;
constexpr int kMinImageWidth = 130;
constexpr int kMinImageHeight = 180;
constexpr double kPairAspect = 0.78;
constexpr double kSingleAspect = 0.70;

// Modes de l'espace de travail (voir FrontendUtils::MODE_NAMES).
constexpr int kModeGrid = 1;
constexpr int kModeMasks = 2;
constexpr int kModePair = 3;
constexpr int kModeAssembly = 4;
constexpr int kModeExport = 5;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
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

void ArtworkCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#484848"));

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
    if (!imagePaths.isEmpty())
        drawPhoto(p, firstImageRect, imagePaths.first());
    else
        drawPainting(p, firstImageRect);
    if (pair) {
        const QRect secondImageRect(x0 + imageW + gutter, y0, imageW, imageH);
        if (imagePaths.size() > 1)
            drawPhoto(p, secondImageRect, imagePaths.at(1));
        else
            drawPainting(p, secondImageRect);
        p.setPen(QPen(QColor("#39b9e8"), 2));
        for (int i = 0; i < 10; ++i) {
            const QPoint a(x0 + imageW - 10 + (i * 31) % qMax(1, imageW / 3),
                           y0 + imageH / 3 + i * imageH / 20);
            const QPoint b(x0 + imageW + gutter + 15 + (i * 37) % qMax(1, imageW / 3),
                           a.y() + (i % 3 - 1) * 8);
            p.drawEllipse(a, 3, 3);
            p.drawEllipse(b, 3, 3);
            p.drawLine(a, b);
        }
        p.setPen(QColor("#eeeeee"));
        p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft, tr("Tuile A  r00_c00"));
        p.drawText(QRect(x0 + imageW + gutter, y0 - 24, imageW, 20),
                   Qt::AlignLeft, tr("Tuile B  r00_c01"));
        return;
    }

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
                   tr("Masque d'exclusion — proposition"));
    } else if (modeValue == kModeAssembly || modeValue == kModeExport) {
        p.setPen(QPen(QColor("#df4038"), 2));
        QPainterPath seam;
        seam.moveTo(x0 + imageW * 0.72, y0);
        seam.cubicTo(x0 + imageW * 0.68, y0 + imageH * .20,
                     x0 + imageW * .83, y0 + imageH * .29,
                     x0 + imageW * .65, y0 + imageH * .44);
        seam.cubicTo(x0 + imageW * .58, y0 + imageH * .70,
                     x0 + imageW * .76, y0 + imageH * .76,
                     x0 + imageW * .69, y0 + imageH);
        p.drawPath(seam);
        if (comparison) {
            p.setPen(QPen(QColor("#f0cf36"), 2));
            p.drawLine(x0 + imageW / 2, y0, x0 + imageW / 2, y0 + imageH);
            p.fillRect(QRect(x0, y0, imageW / 2, imageH), QColor(225, 188, 40, 30));
        }
        p.setPen(QPen(QColor("#e4ca37"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(x0, y0, imageW, imageH));
        p.drawRect(QRect(x0 + imageW * .58, y0, imageW * .20, imageH));
        if (tileContoursVisible)
            drawTileContours(p, firstImageRect);
    }

    p.setPen(QColor("#dddddd"));
    p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
               QString::number(zoomPercent) + QStringLiteral(" %"));
}

QVector<ArtworkCanvas::TileContour> ArtworkCanvas::computeTileContours(
    const QRectF &mosaicRect) const
{
    QVector<TileContour> contours;
    if (mosaicRect.width() <= 0.0 || mosaicRect.height() <= 0.0)
        return contours;

    if (!tilePositions.isEmpty()) {
        double minX = tilePositions.first().x;
        double minY = tilePositions.first().y;
        double maxX = minX;
        double maxY = minY;
        for (const TilePosition &position : tilePositions) {
            const double tileWidth = position.width > 0.0 ? position.width : 4000.0;
            const double tileHeight = position.height > 0.0 ? position.height : 4000.0;
            minX = qMin(minX, position.x);
            minY = qMin(minY, position.y);
            maxX = qMax(maxX, position.x + tileWidth);
            maxY = qMax(maxY, position.y + tileHeight);
        }

        const double spanX = qMax(1.0, maxX - minX);
        const double spanY = qMax(1.0, maxY - minY);
        for (const TilePosition &position : tilePositions) {
            const double tileWidth = position.width > 0.0 ? position.width : 4000.0;
            const double tileHeight = position.height > 0.0 ? position.height : 4000.0;
            const double x = (position.x - minX) / spanX;
            const double y = (position.y - minY) / spanY;
            contours.append({
                position.tileId,
                QRectF(mosaicRect.x() + x * mosaicRect.width(),
                       mosaicRect.y() + y * mosaicRect.height(),
                       tileWidth / spanX * mosaicRect.width(),
                       tileHeight / spanY * mosaicRect.height())
            });
        }
        return contours;
    }

    const int rows = qMax(1, gridRows);
    const int columns = qMax(1, gridColumns);
    const double overlapX = qBound(0.0, horizontalOverlapPercent / 100.0, 0.90);
    const double overlapY = qBound(0.0, verticalOverlapPercent / 100.0, 0.90);
    const double stepX = 1.0 - overlapX;
    const double stepY = 1.0 - overlapY;
    const double totalWidth = 1.0 + (columns - 1) * stepX;
    const double totalHeight = 1.0 + (rows - 1) * stepY;
    const double tileWidth = 1.0 / totalWidth;
    const double tileHeight = 1.0 / totalHeight;

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            if (index < gridTiles.size() && gridTiles.at(index).empty)
                continue;

            const QString tileId = QStringLiteral("r%1_c%2")
                .arg(row, 2, 10, QLatin1Char('0'))
                .arg(column, 2, 10, QLatin1Char('0'));
            contours.append({
                tileId,
                QRectF(mosaicRect.x() + column * stepX / totalWidth * mosaicRect.width(),
                       mosaicRect.y() + row * stepY / totalHeight * mosaicRect.height(),
                       tileWidth * mosaicRect.width(),
                       tileHeight * mosaicRect.height())
            });
        }
    }
    return contours;
}

void ArtworkCanvas::drawTileContours(QPainter &p, const QRect &mosaicRect)
{
    const QVector<TileContour> contours = computeTileContours(QRectF(mosaicRect));
    if (contours.isEmpty())
        return;

    p.save();
    p.setClipRect(mosaicRect);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 180), 4));
    for (const TileContour &contour : contours)
        p.drawRect(contour.rect);

    const QFont labelFont = p.font();
    const QFontMetrics metrics(labelFont);
    p.setPen(QPen(QColor("#f4cc4a"), 2));
    for (const TileContour &contour : contours) {
        p.drawRect(contour.rect);
        const int padding = 4;
        const int textWidth = metrics.horizontalAdvance(contour.tileId);
        const int textHeight = metrics.height();
        const QRectF badge(contour.rect.x() + 3, contour.rect.y() + 3,
                           textWidth + 2 * padding, textHeight + 2 * padding);
        if (badge.right() > contour.rect.right() || badge.bottom() > contour.rect.bottom())
            continue;
        p.fillRect(badge, QColor(20, 20, 20, 200));
        p.setPen(QPen(QColor("#f4cc4a"), 1));
        p.drawRect(badge);
        p.setPen(QColor("#ffffff"));
        p.drawText(badge, Qt::AlignCenter, contour.tileId);
        p.setPen(QPen(QColor("#f4cc4a"), 2));
    }
    p.restore();
}
