#ifndef ARTWORKCANVAS_H
#define ARTWORKCANVAS_H

#include "frontendutils.h"
#include "projectgrid.h"

#include <QFontMetrics>
#include <QFileInfo>
#include <QImage>
#include <QLinearGradient>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRect>
#include <QStringList>
#include <QTransform>
#include <QVector>
#include <QWidget>

#include <functional>
#include <utility>

class ArtworkCanvas final : public QWidget
{
public:
    explicit ArtworkCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 260);
        setMouseTracking(true);
    }

    void setMode(int mode) { modeValue = mode; update(); }
    void setZoom(int zoom) { zoomPercent = zoom; update(); }
    void setImagePaths(const QStringList &paths) { imagePaths = paths; update(); }
    void setGridState(int rows, int columns, int order, const QVector<GridTile> &tiles,
                      int selectedCell)
    {
        gridRows = rows;
        gridColumns = columns;
        captureOrder = order;
        gridTiles = tiles;
        selectedGridCell = selectedCell;
        update();
    }
    void setGridCallbacks(std::function<void(int)> selectionChanged,
                          std::function<void(int, int)> cellsSwapped)
    {
        selectionChangedCallback = std::move(selectionChanged);
        cellsSwappedCallback = std::move(cellsSwapped);
    }
    void toggleComparison() { comparison = !comparison; update(); }
    void setComparisonVisible(bool visible) { comparison = visible; update(); }
    void toggleMask() { maskVisible = !maskVisible; update(); }
    void setMaskVisible(bool visible) { maskVisible = visible; update(); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (modeValue == 1 && event->button() == Qt::LeftButton) {
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

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (modeValue == 1 && event->button() == Qt::LeftButton) {
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

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#484848"));

        if (modeValue == 1) {
            drawGrid(p);
            return;
        }

        const bool pair = modeValue == 3;
        const int gutter = pair ? 22 : 0;
        const int maxW = pair ? (width() - 90) / 2 : width() - 130;
        const int maxH = height() - 80;
        const double aspect = pair ? 0.78 : 0.70;
        int imageH = qMin(maxH, int(maxW / aspect));
        int imageW = int(imageH * aspect);
        if (imageW > maxW) {
            imageW = maxW;
            imageH = int(imageW / aspect);
        }
        imageW = qMax(130, imageW);
        imageH = qMax(180, imageH);
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

        if (modeValue == 2 && maskVisible) {
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
        } else if (modeValue == 4 || modeValue == 5) {
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
        }

        p.setPen(QColor("#dddddd"));
        p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                   QString::number(zoomPercent) + QStringLiteral(" %"));
    }

private:
    void drawGrid(QPainter &p)
    {
        const int rows = qMax(1, gridRows);
        const int columns = qMax(1, gridColumns);
        const int cellCount = rows * columns;
        const int gapX = 18;
        const int gapY = 20;
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

            p.fillRect(QRect(cell.left(), cell.bottom() - 21, cell.width(), 21),
                       QColor(0, 0, 0, 170));
            p.setPen(QColor("#ffffff"));
            const QString cellLabel = QStringLiteral("r%1_c%2")
                .arg(row, 2, 10, QLatin1Char('0')).arg(column, 2, 10, QLatin1Char('0'));
            p.drawText(QRect(cell.left() + 6, cell.bottom() - 20, cell.width() - 12, 19),
                       Qt::AlignLeft | Qt::AlignVCenter, cellLabel);
            if (!tile.path.isEmpty() && !tile.empty && cell.width() > 145) {
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
            p.drawEllipse(QRect(cell.left() + 7, cell.top() + 7, 24, 24));
            p.setPen(Qt::white);
            p.drawText(QRect(cell.left() + 7, cell.top() + 7, 24, 24),
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
            const qreal arrowLength = 16.0;
            const qreal arrowAngle = qDegreesToRadians(28.0);
            const QPointF leftWing(
                end.x() - arrowLength * qCos(angle + arrowAngle),
                end.y() - arrowLength * qSin(angle + arrowAngle));
            const QPointF rightWing(
                end.x() - arrowLength * qCos(angle - arrowAngle),
                end.y() - arrowLength * qSin(angle - arrowAngle));
            p.setBrush(routePen.color());
            p.drawPolygon(QPolygonF() << end << leftWing << rightWing);
            p.restore();
        }

        p.setPen(QColor("#eeeeee"));
        p.drawText(QRect(18, 12, width() - 36, 24), Qt::AlignLeft,
                   tr("Grille %1 × %2 — %3 tuile(s) — glisser pour permuter")
                       .arg(rows).arg(columns).arg(activeTileCount));
    }

    int gridCellAt(const QPoint &point) const
    {
        for (int index = 0; index < gridCellRects.size(); ++index)
            if (gridCellRects.at(index).contains(point))
                return index;
        return -1;
    }

    void drawPainting(QPainter &p, const QRect &r)
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

    void drawPhoto(QPainter &p, const QRect &r, const QString &path,
                   int rotation = 0, bool flipped = false)
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
};

#endif
