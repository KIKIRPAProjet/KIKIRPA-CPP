#include "artworkcanvas.h"

#include <QFileInfo>
#include <QFontMetrics>
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
constexpr int kModeAssembly = 4;
constexpr int kModeExport = 5;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
    const auto defaultPairs = FrontendUtils::defaultPairAlignments();
    if (!defaultPairs.isEmpty()) {
        currentPair = defaultPairs.first();
    }
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

    if (modeValue == kModeExport && !m_mosaicPreview.isNull()) {
        const QRect target = rect().adjusted(24, 24, -24, -24);
        // Réduction en conservant le ratio, puis dessin centré à la taille obtenue.
        // (drawImage(target, ...) ré-étirerait l'image pour remplir tout le rectangle.)
        const QImage fitted = m_mosaicPreview.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint topLeft(target.left() + (target.width() - fitted.width()) / 2,
                             target.top() + (target.height() - fitted.height()) / 2);
        p.drawImage(topLeft, fitted);

        const QRect actualMosaicRect(topLeft, fitted.size());
        if (tileContoursVisible && !tilePositions.isEmpty())
            drawTileContours(p, actualMosaicRect);

        // On affiche le pourcentage de zoom en bas à gauche même pour la mosaïque
        p.setPen(QColor("#dddddd"));
        p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                   QString::number(zoomPercent) + QStringLiteral(" %"));
        return; // On arrête de dessiner ici
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
    const int indexA = (!imagePaths.isEmpty() && currentPair.tileIndexA >= 0)
                           ? (currentPair.tileIndexA % imagePaths.size())
                           : 0;
    const int indexB = (!imagePaths.isEmpty() && currentPair.tileIndexB >= 0)
                           ? (currentPair.tileIndexB % imagePaths.size())
                           : qMin(1, qMax(0, static_cast<int>(imagePaths.size()) - 1));

    QRect renderedRect = firstImageRect;
    if (!imagePaths.isEmpty())
        renderedRect = drawPhoto(p, firstImageRect, imagePaths.at(indexA));
    else
        drawPainting(p, firstImageRect);
    if (pair) {
        // Si le bouton « Superposer (différence) » est actif, la tuile B est superposée sur la zone de recouvrement de A
        const int overlapW = qMax(36, imageW * 35 / 100);
        const QRect secondImageRect = pairOverlayMode
            ? QRect(firstImageRect.right() - overlapW, y0, imageW, imageH)
            : QRect(x0 + imageW + gutter, y0, imageW, imageH);

        if (pairOverlayMode)
            p.setOpacity(0.65);
        if (imagePaths.size() > 1)
            drawPhoto(p, secondImageRect, imagePaths.at(indexB));
        else
            drawPainting(p, secondImageRect);
        if (pairOverlayMode)
            p.setOpacity(1.0);

        // Zone de recouvrement (encadré orange en pointillés)
        const QRect overlapRectA(firstImageRect.right() - overlapW, y0, overlapW, imageH);
        const QRect overlapRectB(secondImageRect.left(), y0, overlapW, imageH);
        p.fillRect(overlapRectA, QColor(217, 130, 43, 28));
        if (!pairOverlayMode)
            p.fillRect(overlapRectB, QColor(217, 130, 43, 28));

        QPen overlapPen(QColor("#d9822b"), 1.5, Qt::DashLine);
        p.setPen(overlapPen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(overlapRectA);
        if (!pairOverlayMode)
            p.drawRect(overlapRectB);

        const int inliersToDraw = qBound(4, currentPair.shownInliers, 16);
        const int outliersToDraw = qBound(0, currentPair.shownOutliers, 6);

        // Points rejetés (faible confiance) en pointillés rouges
        QPen outlierLinePen(QColor("#e05a52"), 1.2, Qt::DashLine);
        QPen outlierCirclePen(QColor("#e05a52"), 1.8);
        for (int i = 0; i < outliersToDraw; ++i) {
            const QPoint a(overlapRectA.left() + 10 + (i * 23) % qMax(1, overlapW - 20),
                           y0 + imageH / 5 + i * (imageH / 5));
            const QPoint b(overlapRectB.left() + 12 + (i * 29) % qMax(1, overlapW - 20),
                           a.y() + ((i % 2 == 0) ? -14 : 16));
            p.setPen(outlierCirclePen);
            p.drawEllipse(a, 4, 4);
            p.drawEllipse(b, 4, 4);
            p.setPen(outlierLinePen);
            p.drawLine(a, b);
        }

        // Points retenus (haute confiance) en vert
        QPen inlierLinePen(QColor("#4ec978"), 1.2, Qt::SolidLine);
        QPen inlierCirclePen(QColor("#4ec978"), 1.8);
        for (int i = 0; i < inliersToDraw; ++i) {
            const QPoint a(overlapRectA.left() + 8 + (i * 19) % qMax(1, overlapW - 16),
                           y0 + imageH * 2 / 5 + (i * (imageH / 2)) / qMax(1, inliersToDraw));
            const QPoint b(overlapRectB.left() + 10 + (i * 19) % qMax(1, overlapW - 16),
                           a.y() + (i % 3 - 1) * 3);
            p.setPen(inlierCirclePen);
            p.drawEllipse(a, 4, 4);
            p.drawEllipse(b, 4, 4);
            p.setPen(inlierLinePen);
            p.drawLine(a, b);
        }

        /* À décommenter lors de la connexion au backend (remplace les 2 boucles simulées ci-dessus) ---
        const double wA = qMax(1, currentPair.tileSizeA.width());
        const double hA = qMax(1, currentPair.tileSizeA.height());
        const double wB = qMax(1, currentPair.tileSizeB.width());
        const double hB = qMax(1, currentPair.tileSizeB.height());

        for (int i = 0; i < currentPair.pointsInA.size() && i < currentPair.pointsInB.size(); ++i) {
            const QPointF &ptA = currentPair.pointsInA.at(i);
            const QPointF &ptB = currentPair.pointsInB.at(i);
            const QPoint a(firstImageRect.left()  + qRound(ptA.x() * imageW / wA),
                           firstImageRect.top()   + qRound(ptA.y() * imageH / hA));
            const QPoint b(secondImageRect.left() + qRound(ptB.x() * imageW / wB),
                           secondImageRect.top()  + qRound(ptB.y() * imageH / hB));

            const bool inlier = (i < currentPair.isInlier.size()) ? currentPair.isInlier.at(i) : true;
            p.setPen(inlier ? inlierCirclePen : outlierCirclePen);
            p.drawEllipse(a, 4, 4);
            p.drawEllipse(b, 4, 4);
            p.setPen(inlier ? inlierLinePen : outlierLinePen);
            p.drawLine(a, b);
        }
        */

        const QString tileALabel = currentPair.tileA.isEmpty() ? QStringLiteral("r00_c00") : currentPair.tileA;
        const QString tileBLabel = currentPair.tileB.isEmpty() ? QStringLiteral("r00_c01") : currentPair.tileB;
        p.setPen(QColor("#eeeeee"));
        p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft,
                   tr("Tuile A  %1").arg(tileALabel));
        p.drawText(QRect(secondImageRect.left(), y0 - 24, imageW, 20),
                   Qt::AlignLeft, tr("Tuile B  %1").arg(tileBLabel));

        // Affichage de l'erreur d'alignement par paire sur le canevas (si le calque est activé)
        if (alignmentErrorVisible) {
            const QColor badgeBg = currentPair.isDubious() ? QColor(217, 130, 43, 220) : QColor(56, 158, 92, 220);
            const QRect errorBadgeRect(overlapRectA.left() + 4, y0 + 8,
                                       qMax(130, overlapW * 2), 22);
            p.fillRect(errorBadgeRect, badgeBg);
            p.setPen(currentPair.isDubious() ? QColor("#1c1c1c") : QColor("#ffffff"));
            p.drawText(errorBadgeRect, Qt::AlignCenter,
                       tr("Erreur paire : %1 px")
                           .arg(QString::number(currentPair.optimizationErrorPx, 'f', 2).replace('.', ',')));
        }
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
                   tr("Masque d'exclusion : proposition"));
    } else if (modeValue == kModeAssembly || modeValue == kModeExport) {
        if(seamVisible){
            p.setPen(QPen(QColor("#df4038"), 2));
            QPainterPath seam;
            seam.moveTo(renderedRect.left() + renderedRect.width() * 0.72, renderedRect.top());
            seam.cubicTo(renderedRect.left() + renderedRect.width() * 0.68, renderedRect.top() + renderedRect.height() * .20,
                         renderedRect.left() + renderedRect.width() * 0.83, renderedRect.top() + renderedRect.height() * .29,
                         renderedRect.left() + renderedRect.width() * 0.65, renderedRect.top() + renderedRect.height() * .44);
            seam.cubicTo(renderedRect.left() + renderedRect.width() * .58, renderedRect.top() + renderedRect.height() * .70,
                         renderedRect.left() + renderedRect.width() * .76, renderedRect.top() + renderedRect.height() * .76,
                         renderedRect.left() + renderedRect.width() * .69, renderedRect.bottom());
            p.drawPath(seam);
        }
        if (comparison) {
            p.setPen(QPen(QColor("#f0cf36"), 2));
            p.drawLine(renderedRect.left() + renderedRect.width() / 2, renderedRect.top(),
                       renderedRect.left() + renderedRect.width() / 2, renderedRect.bottom());
            p.fillRect(QRect(renderedRect.left(), renderedRect.top(),
                             renderedRect.width() / 2, renderedRect.height()),
                       QColor(225, 188, 40, 30));
        }
        p.setPen(QPen(QColor("#e4ca37"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(renderedRect);
        if (alignmentErrorVisible) {
            const QRect errorZoneRect(renderedRect.left() + renderedRect.width() * .58,
                                      renderedRect.top(),
                                      renderedRect.width() * .20,
                                      renderedRect.height());
            p.fillRect(errorZoneRect, QColor(228, 202, 55, 35));
            p.drawRect(errorZoneRect);
            p.setPen(QColor("#f0cf36"));
            p.drawText(QRect(errorZoneRect.left() - 20, renderedRect.top() - 22,
                             errorZoneRect.width() + 80, 20),
                       Qt::AlignLeft,
                       tr("Erreur %1 : %2 px")
                           .arg(currentPair.pairLabel(),
                                QString::number(currentPair.optimizationErrorPx, 'f', 2).replace('.', ',')));
        }
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

QVector<ArtworkCanvas::TileContour> ArtworkCanvas::computeTileContours(
    const QRectF &mosaicRect) const
{
    QVector<TileContour> contours;
    if (mosaicRect.width() <= 0.0 || mosaicRect.height() <= 0.0)
        return contours;

    if (!tilePositions.isEmpty()) {
        double defaultTileWidth = 0.0;
        double defaultTileHeight = 0.0;
        for (const TilePosition &pos : tilePositions) {
            if (pos.width > 0.0 && defaultTileWidth <= 0.0)
                defaultTileWidth = pos.width;
            if (pos.height > 0.0 && defaultTileHeight <= 0.0)
                defaultTileHeight = pos.height;
        }
        if (defaultTileWidth <= 0.0)
            defaultTileWidth = 1.0;
        if (defaultTileHeight <= 0.0)
            defaultTileHeight = 1.0;

        double minX = tilePositions.first().x;
        double minY = tilePositions.first().y;
        double maxX = minX;
        double maxY = minY;
        for (const TilePosition &position : tilePositions) {
            const double tileWidth = position.width > 0.0 ? position.width : defaultTileWidth;
            const double tileHeight = position.height > 0.0 ? position.height : defaultTileHeight;
            minX = qMin(minX, position.x);
            minY = qMin(minY, position.y);
            maxX = qMax(maxX, position.x + tileWidth);
            maxY = qMax(maxY, position.y + tileHeight);
        }

        const double spanX = qMax(1.0, maxX - minX);
        const double spanY = qMax(1.0, maxY - minY);
        for (const TilePosition &position : tilePositions) {
            const double tileWidth = position.width > 0.0 ? position.width : defaultTileWidth;
            const double tileHeight = position.height > 0.0 ? position.height : defaultTileHeight;
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
            if (index >= gridTiles.size())
                continue;

            const GridTile &tile = gridTiles.at(index);
            if (tile.empty || tile.path.trimmed().isEmpty())
                continue;

            const QString tileId = QFileInfo(tile.path).fileName();
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

    const QFontMetrics metrics(p.font());
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
