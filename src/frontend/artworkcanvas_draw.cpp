#include "artworkcanvas.h"
#include "frontendutils.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

namespace {
constexpr int kGridGapX = 18;
constexpr int kGridGapY = 20;
constexpr int kLabelBarHeight = 21;
constexpr int kBadgeSize = 24;
constexpr int kMinCellWidthForFileName = 145;
constexpr qreal kArrowLength = 16.0;
constexpr qreal kArrowAngleDegrees = 28.0;
}

void ArtworkCanvas::drawGrid(QPainter &p)
{
    const int rows = qMax(1, gridRows);
    const int columns = qMax(1, gridColumns);
    const int cellCount = rows * columns;
    const int gapX = kGridGapX;
    const int gapY = kGridGapY;
    const QRect area = rect().adjusted(24, 48, -24, -24);
    const int cellW = qMax(1, (area.width() - (columns - 1) * gapX) / columns);
    const int cellH = qMax(1, (area.height() - (rows - 1) * gapY) / rows);
    const int gridW = columns * cellW + (columns - 1) * gapX;
    const int gridH = rows * cellH + (rows - 1) * gapY;
    const int left = area.left() + (area.width() - gridW) / 2;
    const int top = area.top() + (area.height() - gridH) / 2;
    gridCellRects.clear();
    gridCellRects.reserve(cellCount);

    const QVector<int> orderCells = captureOrderCells(rows, columns, captureOrder);
    QVector<int> captureNumbers(cellCount, 0);
    QVector<QPointF> routePoints;
    int sequence = 0;
    for (const int cellIndex : orderCells) {
        if (cellIndex >= gridTiles.size())
            continue;
        const GridTile &tile = gridTiles.at(cellIndex);
        if (tile.path.isEmpty() || tile.empty)
            continue;
        captureNumbers[cellIndex] = ++sequence;
        const int row = cellIndex / columns;
        const int column = cellIndex % columns;
        routePoints.append(QPointF(left + column * (cellW + gapX) + cellW / 2.0,
                                   top + row * (cellH + gapY) + cellH / 2.0));
    }

    int activeTileCount = 0;
    for (int index = 0; index < qMin(cellCount, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTileCount;
    }

    for (int index = 0; index < cellCount; ++index) {
        const int row = index / columns;
        const int column = index % columns;
        const QRect cell(left + column * (cellW + gapX),
                         top + row * (cellH + gapY), cellW, cellH);
        gridCellRects.append(cell);
        const GridTile tile = index < gridTiles.size() ? gridTiles.at(index) : GridTile{};

        if (tile.path.isEmpty() && !tile.empty) {
            if (index == selectedGridCell) {
                p.setPen(QPen(QColor("#4b9bea"), 2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(cell.adjusted(1, 1, -2, -2));
            }
            continue;
        }

        if (!tile.path.isEmpty()) {
            drawPhoto(p, cell, tile.path, tile.rotation, tile.flipped);
            if (tile.empty)
                p.fillRect(cell, QColor(20, 20, 20, 170));
        } else {
            p.fillRect(cell, QColor("#282828"));
        }

        p.fillRect(QRect(cell.left(), cell.bottom() - kLabelBarHeight, cell.width(), kLabelBarHeight),
                   QColor(0, 0, 0, 170));
        p.setPen(QColor("#ffffff"));
        const QString cellLabel = QStringLiteral("r%1_c%2")
            .arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));
        p.drawText(QRect(cell.left() + 6, cell.bottom() - 20, cell.width() - 12, 19),
                   Qt::AlignLeft | Qt::AlignVCenter, cellLabel);
        if (!tile.path.isEmpty() && !tile.empty && cell.width() > kMinCellWidthForFileName) {
            const QString fileName = QFontMetrics(p.font()).elidedText(
                QFileInfo(tile.path).fileName(), Qt::ElideRight, cell.width() / 2);
            p.drawText(QRect(cell.left() + cell.width() / 2, cell.bottom() - 20,
                             cell.width() / 2 - 5, 19),
                       Qt::AlignRight | Qt::AlignVCenter, fileName);
        } else if (tile.path.isEmpty() || tile.empty) {
            p.setPen(tile.empty ? QColor("#e8c638") : QColor("#aaaaaa"));
            p.drawText(cell.adjusted(5, 4, -5, -24), Qt::AlignCenter,
                       tile.empty ? tr("CASE VIDE") : tr("Déposer une tuile ici"));
        }

        p.setPen(QPen(index == selectedGridCell ? QColor("#4b9bea") : QColor("#e8c638"),
                      index == selectedGridCell ? 3 : 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell.adjusted(1, 1, -2, -2));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#4b8bd6"));
        p.drawEllipse(QRect(cell.left() + 7, cell.top() + 7, kBadgeSize, kBadgeSize));
        p.setPen(Qt::white);
        p.drawText(QRect(cell.left() + 7, cell.top() + 7, kBadgeSize, kBadgeSize),
                   Qt::AlignCenter, QString::number(captureNumbers.at(index)));
    }

    if (routePoints.size() > 1) {
        p.save();
        QPen routePen(QColor(244, 204, 74, 225), 3, Qt::DashLine, Qt::RoundCap,
                      Qt::RoundJoin);
        p.setPen(routePen);
        for (int index = 1; index < routePoints.size(); ++index)
            p.drawLine(routePoints.at(index - 1), routePoints.at(index));

        const QPointF end = routePoints.last();
        const QLineF finalSegment(routePoints.at(routePoints.size() - 2), end);
        const qreal angle = qAtan2(finalSegment.dy(), finalSegment.dx());
        const qreal arrowAngle = qDegreesToRadians(kArrowAngleDegrees);
        const QPointF leftWing(
            end.x() - kArrowLength * qCos(angle + arrowAngle),
            end.y() - kArrowLength * qSin(angle + arrowAngle));
        const QPointF rightWing(
            end.x() - kArrowLength * qCos(angle - arrowAngle),
            end.y() - kArrowLength * qSin(angle - arrowAngle));
        p.setBrush(routePen.color());
        p.drawPolygon(QPolygonF() << end << leftWing << rightWing);
        p.restore();
    }

    p.setPen(QColor("#eeeeee"));
    p.drawText(QRect(18, 12, width() - 36, 24), Qt::AlignLeft,
               tr("Grille %1 × %2 — %3 tuile(s) — glisser pour permuter")
                   .arg(rows).arg(columns).arg(activeTileCount));
}

void ArtworkCanvas::drawPainting(QPainter &p, const QRect &r)
{
    p.save();
    p.setClipRect(r);
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0, QColor("#b6b5af"));
    bg.setColorAt(.55, QColor("#777872"));
    bg.setColorAt(1, QColor("#343532"));
    p.fillRect(r, bg);
    p.fillRect(r.adjusted(0, 0, 0, -int(r.height() * .80)), QColor("#171817"));

    const double sx = r.width() / 240.0;
    const double sy = r.height() / 360.0;
    p.translate(r.topLeft());
    p.scale(sx, sy);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#393a37"));
    QPainterPath body;
    body.moveTo(34, 360);
    body.cubicTo(38, 260, 64, 214, 101, 208);
    body.cubicTo(158, 198, 205, 276, 218, 360);
    body.closeSubpath();
    p.drawPath(body);
    p.setBrush(QColor("#b8b7b0"));
    p.drawEllipse(QRectF(88, 92, 68, 96));
    p.setBrush(QColor("#222321"));
    QPainterPath hair;
    hair.moveTo(84, 142);
    hair.cubicTo(69, 76, 82, 28, 132, 32);
    hair.cubicTo(184, 36, 193, 94, 168, 147);
    hair.cubicTo(166, 99, 140, 69, 104, 91);
    hair.closeSubpath();
    p.drawPath(hair);
    p.setBrush(QColor("#d4d1c7"));
    QPainterPath hat;
    hat.moveTo(68, 88);
    hat.cubicTo(75, 38, 158, 31, 181, 75);
    hat.lineTo(201, 102);
    hat.cubicTo(151, 85, 105, 83, 68, 101);
    hat.closeSubpath();
    p.drawPath(hat);
    p.setPen(QPen(QColor("#484945"), 2));
    p.drawLine(103, 137, 119, 135);
    p.drawLine(136, 135, 151, 137);
    p.drawLine(120, 140, 116, 159);
    p.drawLine(115, 169, 132, 170);
    p.setPen(QPen(QColor("#aaa9a2"), 1));
    for (int y = 210; y < 350; y += 14) {
        p.drawLine(48, y, 198, y - 8);
    }
    p.restore();

    p.setPen(QPen(QColor("#101010"), 5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

void ArtworkCanvas::drawPhoto(QPainter &p, const QRect &r, const QString &path,
                              int rotation, bool flipped)
{
    QSize previewSize = r.size();
    if (rotation % 180 != 0)
        previewSize.transpose();
    QImage image = readPreviewImage(path, previewSize);
    if (image.isNull()) {
        p.fillRect(r, QColor("#262626"));
        p.setPen(QColor("#d35b55"));
        p.drawText(r, Qt::AlignCenter, tr("Image illisible"));
        return;
    }
    if (flipped)
        image = image.mirrored(true, false);
    if (rotation != 0)
        image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    if (image.width() > r.width() || image.height() > r.height())
        image = image.scaled(r.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    p.save();
    p.setClipRect(r);
    const int x = r.left() + (r.width() - image.width()) / 2;
    const int y = r.top() + (r.height() - image.height()) / 2;
    p.drawImage(x, y, image);
    p.restore();
    p.setPen(QPen(QColor("#101010"), 5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}




bool ArtworkCanvas::drawImageLayers(QPainter &p, const QRect &area)
{
    QRectF worldBounds;
    bool firstLayer = true;

    // 1. Calculer la zone occupée par toutes les images
    for (const ImageLayer &layer : imageLayers) {
        if (layer.image.isNull() || layer.worldRect.isEmpty())
            continue;

        if (firstLayer) {
            worldBounds = layer.worldRect;
            firstLayer = false;
        } else {
            worldBounds = worldBounds.united(layer.worldRect);
        }
    }

    if (firstLayer || worldBounds.isEmpty())
        return false;

    // 2. Adapter la mosaïque à la zone d'affichage
    const qreal scale = qMin(
        qreal(area.width()) / worldBounds.width(),
        qreal(area.height()) / worldBounds.height()
    );

    const qreal offsetX =
        area.center().x() - worldBounds.width() * scale / 2.0;

    const qreal offsetY =
        area.center().y() - worldBounds.height() * scale / 2.0;

    // 3. Dessiner les couches dans leur ordre
    p.save();
    p.setClipRect(area);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    for (const ImageLayer &layer : imageLayers) {
        if (!layer.visible || layer.image.isNull()
            || layer.worldRect.isEmpty())
            continue;

        const QRectF targetRect(
            offsetX + (layer.worldRect.x() - worldBounds.x()) * scale,
            offsetY + (layer.worldRect.y() - worldBounds.y()) * scale,
            layer.worldRect.width() * scale,
            layer.worldRect.height() * scale
        );

        p.setOpacity(qBound(0.0, layer.opacity, 1.0));
        p.drawImage(targetRect, layer.image);
    }

    p.restore();
    return true;
}
