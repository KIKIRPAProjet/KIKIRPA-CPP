// src/frontend/artworkcanvas.cpp
#include "artworkcanvas.h"

#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QWheelEvent>

#include <utility>

namespace {
constexpr int kMinimumWidth = 360;
constexpr int kMinimumHeight = 120;
constexpr int kPairGutter = 22;
constexpr int kMinImageWidth = 130;
constexpr int kMinImageHeight = 180;
constexpr double kPairAspect = 0.78;
constexpr double kSingleAspect = 0.70;
constexpr int kMinZoomPercent = 5;
constexpr int kMaxZoomPercent = 1600;
constexpr double kZoomWheelFactor = 1.15;
constexpr qsizetype kImageCacheMaxBytes = 256 * 1024 * 1024;

// Modes actuels de main.
constexpr int kModeGrid = 1;
constexpr int kModeMasks = 2;
constexpr int kModePair = 3;
constexpr int MODE_SEAM = 4;
constexpr int kModeAssembly = 5;
constexpr int kModeExport = 6;

// Index de l'outil Recadrer.
constexpr int kCropTool = 9;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_imageCache.setMaxCost(kImageCacheMaxBytes);
}

void ArtworkCanvas::setCurrentPair(
    const FrontendUtils::PairAlignmentInfo &pair)
{
    currentPair = pair;
    update();
}

QTransform ArtworkCanvas::viewTransform() const
{
    const qreal scale = zoomPercent / 100.0;
    const QPointF viewCenter(width() / 2.0, height() / 2.0);

    QTransform transform;
    transform.translate(
        viewCenter.x() + m_panOffset.x(),
        viewCenter.y() + m_panOffset.y());
    transform.scale(scale, scale);
    transform.translate(-viewCenter.x(), -viewCenter.y());

    return transform;
}

void ArtworkCanvas::resetView()
{
    zoomPercent = 100;
    m_panOffset = QPointF(0.0, 0.0);
    update();
}

void ArtworkCanvas::setGridState(
    int rows,
    int columns,
    int order,
    const QVector<GridTile> &tiles,
    int selectedCell)
{
    gridRows = rows;
    gridColumns = columns;
    captureOrder = order;
    gridTiles = tiles;
    selectedGridCell = selectedCell;
    update();
}

void ArtworkCanvas::setGridCallbacks(
    std::function<void(int)> selectionChanged,
    std::function<void(int, int)> cellsSwapped)
{
    selectionChangedCallback = std::move(selectionChanged);
    cellsSwappedCallback = std::move(cellsSwapped);
}

bool ArtworkCanvas::hasActiveImages() const
{
    if (!imagePaths.isEmpty())
        return true;

    for (const GridTile &tile : gridTiles) {
        if (!tile.path.isEmpty())
            return true;
    }

    return false;
}

QRect ArtworkCanvas::emptyImportCardRect() const
{
    const int cardW = qMin(340, qMax(220, width() - 48));
    const int cardH = qMin(190, qMax(140, height() - 48));

    return QRect(
        (width() - cardW) / 2,
        (height() - cardH) / 2,
        cardW,
        cardH);
}

ArtworkCanvas::CropHandle ArtworkCanvas::hitTestCropHandle(
    const QPoint &pos) const
{
    if (!m_cropSelectionRect.isValid() ||
        m_cropSelectionRect.isEmpty()) {
        return CropHandle::None;
    }

    constexpr int margin = 12;
    const QRect &cropRect = m_cropSelectionRect;

    if (QRect(
            cropRect.left() - margin,
            cropRect.top() - margin,
            2 * margin,
            2 * margin).contains(pos)) {
        return CropHandle::TopLeft;
    }

    if (QRect(
            cropRect.right() - margin,
            cropRect.top() - margin,
            2 * margin,
            2 * margin).contains(pos)) {
        return CropHandle::TopRight;
    }

    if (QRect(
            cropRect.left() - margin,
            cropRect.bottom() - margin,
            2 * margin,
            2 * margin).contains(pos)) {
        return CropHandle::BottomLeft;
    }

    if (QRect(
            cropRect.right() - margin,
            cropRect.bottom() - margin,
            2 * margin,
            2 * margin).contains(pos)) {
        return CropHandle::BottomRight;
    }

    if (qAbs(pos.y() - cropRect.top()) <= margin &&
        pos.x() >= cropRect.left() &&
        pos.x() <= cropRect.right()) {
        return CropHandle::Top;
    }

    if (qAbs(pos.y() - cropRect.bottom()) <= margin &&
        pos.x() >= cropRect.left() &&
        pos.x() <= cropRect.right()) {
        return CropHandle::Bottom;
    }

    if (qAbs(pos.x() - cropRect.left()) <= margin &&
        pos.y() >= cropRect.top() &&
        pos.y() <= cropRect.bottom()) {
        return CropHandle::Left;
    }

    if (qAbs(pos.x() - cropRect.right()) <= margin &&
        pos.y() >= cropRect.top() &&
        pos.y() <= cropRect.bottom()) {
        return CropHandle::Right;
    }

    if (cropRect.contains(pos))
        return CropHandle::Move;

    return CropHandle::None;
}

void ArtworkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (!hasActiveImages()) {
        if (event->button() == Qt::LeftButton &&
            emptyImportCardRect().contains(event->pos())) {
            emit importTilesRequested();
            event->accept();
            return;
        }
    }

    setFocus();

    if (modeValue == kModeGrid &&
        event->button() == Qt::LeftButton) {
        dragStartCell = gridCellAt(event->pos());

        if (dragStartCell >= 0) {
            dragTargetCell = dragStartCell;
            selectedGridCell = dragStartCell;
            setCursor(Qt::ClosedHandCursor);

            if (selectionChangedCallback)
                selectionChangedCallback(selectedGridCell);

            update();
            event->accept();
            return;
        }
    }

    if (modeValue == kModeExport &&
        m_activeTool == kCropTool &&
        event->button() == Qt::LeftButton) {
        const QPoint scenePos =
            viewTransform().inverted().map(event->pos());

        const QRect displayedImageRect = mosaicDisplayedRect();

        if (displayedImageRect.contains(scenePos)) {
            m_cropDragStart = scenePos;
            m_cropRectAtDragStart = m_cropSelectionRect;
            m_activeHandle = hitTestCropHandle(scenePos);
            m_isCropping = true;

            if (m_activeHandle == CropHandle::None) {
                m_cropStartPos = scenePos;
                m_cropSelectionRect =
                    QRect(m_cropStartPos, QSize(0, 0));
            }

            update();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ArtworkCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (modeValue == kModeExport &&
        m_activeTool == kCropTool) {
        const QPoint scenePos =
            viewTransform().inverted().map(event->pos());

        if (m_cropSelectionRect.isValid() &&
            m_cropSelectionRect.contains(scenePos)) {
            applyCrop();
            event->accept();
            return;
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void ArtworkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!hasActiveImages()) {
        const bool hovered =
            emptyImportCardRect().contains(event->pos());

        if (hovered != m_emptyCardHovered) {
            m_emptyCardHovered = hovered;

            if (hovered)
                setCursor(Qt::PointingHandCursor);
            else
                unsetCursor();

            update();
        }

        event->accept();
        return;
    }

    if (modeValue == kModeGrid && dragStartCell >= 0) {
        const int hovered = gridCellAt(event->pos());

        if (hovered != dragTargetCell) {
            dragTargetCell = hovered;
            update();
        }

        event->accept();
        return;
    }

    if (m_panning) {
        m_panOffset += event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        update();
        event->accept();
        return;
    }

    if (modeValue == kModeExport &&
        m_activeTool == kCropTool) {
        const QPoint scenePos =
            viewTransform().inverted().map(event->pos());

        const QRect displayedImageRect = mosaicDisplayedRect();

        const int clampedX = qBound(
            displayedImageRect.left(),
            scenePos.x(),
            displayedImageRect.right());

        const int clampedY = qBound(
            displayedImageRect.top(),
            scenePos.y(),
            displayedImageRect.bottom());

        const QPoint curPos(clampedX, clampedY);

        if (m_isCropping) {
            const QPoint delta = curPos - m_cropDragStart;

            if (m_activeHandle == CropHandle::None) {
                m_cropSelectionRect =
                    QRect(m_cropStartPos, curPos).normalized();
            } else if (m_activeHandle == CropHandle::Move) {
                QRect moved = m_cropRectAtDragStart.translated(delta);

                if (moved.left() < displayedImageRect.left())
                    moved.moveLeft(displayedImageRect.left());

                if (moved.top() < displayedImageRect.top())
                    moved.moveTop(displayedImageRect.top());

                if (moved.right() > displayedImageRect.right())
                    moved.moveRight(displayedImageRect.right());

                if (moved.bottom() > displayedImageRect.bottom())
                    moved.moveBottom(displayedImageRect.bottom());

                m_cropSelectionRect = moved;
            } else {
                QRect adjustedCropRect = m_cropRectAtDragStart;

                switch (m_activeHandle) {
                case CropHandle::TopLeft:
                    adjustedCropRect.setTop(
                        qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                    adjustedCropRect.setLeft(
                        qMin(curPos.x(), adjustedCropRect.right() - 15));
                    break;

                case CropHandle::TopRight:
                    adjustedCropRect.setTop(
                        qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                    adjustedCropRect.setRight(
                        qMax(curPos.x(), adjustedCropRect.left() + 15));
                    break;

                case CropHandle::BottomLeft:
                    adjustedCropRect.setBottom(
                        qMax(curPos.y(), adjustedCropRect.top() + 15));
                    adjustedCropRect.setLeft(
                        qMin(curPos.x(), adjustedCropRect.right() - 15));
                    break;

                case CropHandle::BottomRight:
                    adjustedCropRect.setBottom(
                        qMax(curPos.y(), adjustedCropRect.top() + 15));
                    adjustedCropRect.setRight(
                        qMax(curPos.x(), adjustedCropRect.left() + 15));
                    break;

                case CropHandle::Top:
                    adjustedCropRect.setTop(
                        qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                    break;

                case CropHandle::Bottom:
                    adjustedCropRect.setBottom(
                        qMax(curPos.y(), adjustedCropRect.top() + 15));
                    break;

                case CropHandle::Left:
                    adjustedCropRect.setLeft(
                        qMin(curPos.x(), adjustedCropRect.right() - 15));
                    break;

                case CropHandle::Right:
                    adjustedCropRect.setRight(
                        qMax(curPos.x(), adjustedCropRect.left() + 15));
                    break;

                default:
                    break;
                }

                m_cropSelectionRect =
                    adjustedCropRect.intersected(displayedImageRect);
            }

            update();
            event->accept();
            return;
        }

        if (m_cropSelectionRect.isValid() &&
            !m_cropSelectionRect.isEmpty()) {
            const CropHandle handle = hitTestCropHandle(scenePos);

            switch (handle) {
            case CropHandle::TopLeft:
            case CropHandle::BottomRight:
                setCursor(Qt::SizeFDiagCursor);
                break;

            case CropHandle::TopRight:
            case CropHandle::BottomLeft:
                setCursor(Qt::SizeBDiagCursor);
                break;

            case CropHandle::Top:
            case CropHandle::Bottom:
                setCursor(Qt::SizeVerCursor);
                break;

            case CropHandle::Left:
            case CropHandle::Right:
                setCursor(Qt::SizeHorCursor);
                break;

            case CropHandle::Move:
                setCursor(Qt::SizeAllCursor);
                break;

            case CropHandle::None:
                setCursor(Qt::CrossCursor);
                break;
            }
        } else {
            setCursor(Qt::CrossCursor);
        }
    } else {
        unsetCursor();
    }

    QWidget::mouseMoveEvent(event);
}

void ArtworkCanvas::leaveEvent(QEvent *event)
{
    if (m_emptyCardHovered) {
        m_emptyCardHovered = false;
        unsetCursor();
        update();
    }

    QWidget::leaveEvent(event);
}

void ArtworkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (modeValue == kModeGrid &&
        event->button() == Qt::LeftButton) {
        const int targetCell = gridCellAt(event->pos());

        if (dragStartCell >= 0 &&
            targetCell >= 0 &&
            targetCell != dragStartCell) {
            if (cellsSwappedCallback)
                cellsSwappedCallback(dragStartCell, targetCell);
        }

        dragStartCell = -1;
        dragTargetCell = -1;
        unsetCursor();
        event->accept();
        return;
    }

    if (m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (modeValue == kModeExport &&
        m_activeTool == kCropTool &&
        event->button() == Qt::LeftButton &&
        m_isCropping) {
        m_isCropping = false;
        m_activeHandle = CropHandle::None;

        if (m_cropSelectionRect.width() < 15 ||
            m_cropSelectionRect.height() < 15) {
            m_cropSelectionRect = QRect();
        }

        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ArtworkCanvas::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();

    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    const qreal factor =
        delta > 0 ? kZoomWheelFactor : 1.0 / kZoomWheelFactor;

    const int newZoom = qBound(
        kMinZoomPercent,
        qRound(zoomPercent * factor),
        kMaxZoomPercent);

    if (newZoom != zoomPercent) {
        const qreal oldScale = zoomPercent / 100.0;
        const qreal newScale = newZoom / 100.0;
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF mousePos = event->position();

        m_panOffset =
            mousePos - center -
            (mousePos - center - m_panOffset) * (newScale / oldScale);

        zoomPercent = newZoom;
        emit zoomChanged(zoomPercent);
        update();
    }

    event->accept();
}

void ArtworkCanvas::keyPressEvent(QKeyEvent *event)
{
    if (modeValue == kModeExport &&
        m_activeTool == kCropTool) {
        if (event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Enter) {
            applyCrop();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Escape) {
            cancelCropSelection();
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

int ArtworkCanvas::gridCellAt(const QPoint &point) const
{
    for (int index = 0; index < gridCellRects.size(); ++index) {
        if (gridCellRects.at(index).contains(point))
            return index;
    }

    return -1;
}

void ArtworkCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor("#484848"));

    const qreal scale = zoomPercent / 100.0;
    const QPointF viewCenter(width() / 2.0, height() / 2.0);

    auto beginViewTransform = [&]() {
        p.save();
        p.translate(viewCenter + m_panOffset);
        p.scale(scale, scale);
        p.translate(-viewCenter);
    };

    if (modeValue == kModeExport) {
        if (!m_mosaicPreview.isNull()) {
            const QRect displayedRect = mosaicDisplayedRect();

            beginViewTransform();
            p.drawImage(QRectF(displayedRect), m_mosaicPreview);

            // Contours réels, uniquement sur une image non recadrée.
            if (tileContoursVisible &&
                !tilePositions.isEmpty() &&
                !m_isCropped) {
                drawTileContours(p, displayedRect);
            }

            if (m_activeTool == kCropTool)
                drawCropOverlay(p, displayedRect);

            p.restore();

            p.setPen(QColor("#dddddd"));
            p.drawText(
                QRect(14, height() - 30, 160, 20),
                Qt::AlignLeft,
                QString::number(zoomPercent) + QStringLiteral(" %"));
            return;
        }

        QFont font = p.font();
        font.setPointSize(13);
        p.setFont(font);
        p.setPen(QColor("#b0b0b0"));
        p.drawText(
            rect(),
            Qt::AlignCenter,
            tr("Un peu de patience : l'image n'est pas encore prête"));
        return;
    }

    if (!hasActiveImages()) {
        drawEmptyState(p);
        return;
    }

    if (modeValue == kModeAssembly ||
        modeValue == MODE_SEAM) {
        beginViewTransform();

        const bool drawn =
            drawMosaicPreview(p, modeValue == MODE_SEAM || seamVisible);

        if (drawn &&
            modeValue == kModeAssembly &&
            tileContoursVisible &&
            !tilePositions.isEmpty() &&
            !m_isCropped) {
            drawTileContours(p, mosaicDisplayedRect());
        }

        p.restore();

        if (drawn) {
            p.setPen(QColor("#dddddd"));
            p.drawText(
                QRect(14, height() - 30, 160, 20),
                Qt::AlignLeft,
                QString::number(zoomPercent) + QStringLiteral(" %"));
            return;
        }
    }

    if (modeValue == MODE_SEAM) {
        drawCenteredMessage(
            p,
            tr("Lancez l'assemblage pour afficher les lignes de couture calculées."));
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

    beginViewTransform();

    if (pair) {
        drawPairView(p);
        p.restore();
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
        p.drawText(
            QRect(x0, y0 - 24, imageW, 20),
            Qt::AlignLeft,
            tr("Masque d'exclusion : proposition"));
    } else if (modeValue == kModeAssembly) {
        // Sans mosaïque calculée, aucune couture simulée.
        if (comparison) {
            p.setPen(QPen(QColor("#f0cf36"), 2));
            p.drawLine(
                x0 + imageW / 2,
                y0,
                x0 + imageW / 2,
                y0 + imageH);
            p.fillRect(
                QRect(x0, y0, imageW / 2, imageH),
                QColor(225, 188, 40, 30));
        }

        p.setPen(QPen(QColor("#e4ca37"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(firstImageRect);
        p.setPen(QColor("#dddddd"));
        p.drawText(
            QRect(x0, y0 - 24, imageW, 20),
            Qt::AlignLeft,
            tr("La mosaïque s'affichera ici après l'assemblage."));
    }

    p.restore();

    p.setPen(QColor("#dddddd"));
    p.drawText(
        QRect(14, height() - 30, 160, 20),
        Qt::AlignLeft,
        QString::number(zoomPercent) + QStringLiteral(" %"));
}

void ArtworkCanvas::setSeamVisible(bool visible)
{
    if (seamVisible != visible) {
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

    if (mosaicRect.width() <= 0.0 ||
        mosaicRect.height() <= 0.0) {
        return contours;
    }

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
            const double tileWidth =
                position.width > 0.0
                    ? position.width
                    : defaultTileWidth;

            const double tileHeight =
                position.height > 0.0
                    ? position.height
                    : defaultTileHeight;

            minX = qMin(minX, position.x);
            minY = qMin(minY, position.y);
            maxX = qMax(maxX, position.x + tileWidth);
            maxY = qMax(maxY, position.y + tileHeight);
        }

        const double spanX = qMax(1.0, maxX - minX);
        const double spanY = qMax(1.0, maxY - minY);

        for (const TilePosition &position : tilePositions) {
            const double tileWidth =
                position.width > 0.0
                    ? position.width
                    : defaultTileWidth;

            const double tileHeight =
                position.height > 0.0
                    ? position.height
                    : defaultTileHeight;

            const double x = (position.x - minX) / spanX;
            const double y = (position.y - minY) / spanY;

            contours.append({
                position.tileId,
                QRectF(
                    mosaicRect.x() + x * mosaicRect.width(),
                    mosaicRect.y() + y * mosaicRect.height(),
                    tileWidth / spanX * mosaicRect.width(),
                    tileHeight / spanY * mosaicRect.height())
            });
        }

        return contours;
    }

    const int rows = qMax(1, gridRows);
    const int columns = qMax(1, gridColumns);
    const double overlapX =
        qBound(0.0, horizontalOverlapPercent / 100.0, 0.90);
    const double overlapY =
        qBound(0.0, verticalOverlapPercent / 100.0, 0.90);
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
                QRectF(
                    mosaicRect.x() +
                        column * stepX / totalWidth * mosaicRect.width(),
                    mosaicRect.y() +
                        row * stepY / totalHeight * mosaicRect.height(),
                    tileWidth * mosaicRect.width(),
                    tileHeight * mosaicRect.height())
            });
        }
    }

    return contours;
}

void ArtworkCanvas::drawTileContours(
    QPainter &p,
    const QRect &mosaicRect)
{
    const QVector<TileContour> contours =
        computeTileContours(QRectF(mosaicRect));

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

        const QRectF badge(
            contour.rect.x() + 3,
            contour.rect.y() + 3,
            textWidth + 2 * padding,
            textHeight + 2 * padding);

        if (badge.right() > contour.rect.right() ||
            badge.bottom() > contour.rect.bottom()) {
            continue;
        }

        p.fillRect(badge, QColor(20, 20, 20, 200));
        p.setPen(QPen(QColor("#f4cc4a"), 1));
        p.drawRect(badge);
        p.setPen(QColor("#ffffff"));
        p.drawText(badge, Qt::AlignCenter, contour.tileId);
        p.setPen(QPen(QColor("#f4cc4a"), 2));
    }

    p.restore();
}

void ArtworkCanvas::setMosaicPreview(const QImage &image)
{
    m_mosaicPreview = image;
    m_originalMosaicPreview = image;
    m_isCropped = false;
    m_cropNormalizedRect = QRectF(0, 0, 1, 1);
    m_cropSelectionRect = QRect();
    update();
}

QRect ArtworkCanvas::mosaicDisplayedRect() const
{
    if (m_mosaicPreview.isNull())
        return QRect();

    const QRect target = rect().adjusted(24, 24, -24, -24);

    const QSize scaledSize =
        m_mosaicPreview.size().scaled(
            target.size(),
            Qt::KeepAspectRatio);

    return QRect(
        target.left() + (target.width() - scaledSize.width()) / 2,
        target.top() + (target.height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height());
}

void ArtworkCanvas::drawCropOverlay(
    QPainter &painter,
    const QRect &imageRect)
{
    if (!m_cropSelectionRect.isValid() ||
        m_cropSelectionRect.isEmpty()) {
        painter.setPen(QColor(240, 240, 240, 180));
        painter.drawText(
            QRect(
                imageRect.left(),
                imageRect.top() + 10,
                imageRect.width(),
                24),
            Qt::AlignCenter,
            tr("Cliquez et glissez sur l'image pour définir la zone à recadrer"));
        return;
    }

    const QRegion fullRegion(imageRect);
    const QRegion cropRegion(m_cropSelectionRect);

    painter.setClipRegion(fullRegion.subtracted(cropRegion));
    painter.fillRect(imageRect, QColor(0, 0, 0, 150));
    painter.setClipping(false);

    painter.setPen(QPen(QColor("#ffffff"), 1, Qt::DashLine));
    painter.drawRect(m_cropSelectionRect);

    painter.setPen(QPen(QColor("#f0cf36"), 3));

    constexpr int handleLength = 12;
    const auto &selection = m_cropSelectionRect;

    painter.drawLine(
        selection.left(), selection.top(),
        selection.left() + handleLength, selection.top());
    painter.drawLine(
        selection.left(), selection.top(),
        selection.left(), selection.top() + handleLength);

    painter.drawLine(
        selection.right(), selection.top(),
        selection.right() - handleLength, selection.top());
    painter.drawLine(
        selection.right(), selection.top(),
        selection.right(), selection.top() + handleLength);

    painter.drawLine(
        selection.left(), selection.bottom(),
        selection.left() + handleLength, selection.bottom());
    painter.drawLine(
        selection.left(), selection.bottom(),
        selection.left(), selection.bottom() - handleLength);

    painter.drawLine(
        selection.right(), selection.bottom(),
        selection.right() - handleLength, selection.bottom());
    painter.drawLine(
        selection.right(), selection.bottom(),
        selection.right(), selection.bottom() - handleLength);

    constexpr int squareSize = 8;
    painter.setBrush(QColor("#f0cf36"));
    painter.setPen(QPen(QColor("#222222"), 1));

    painter.drawRect(
        selection.left() - squareSize / 2,
        selection.top() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.right() - squareSize / 2,
        selection.top() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.left() - squareSize / 2,
        selection.bottom() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.right() - squareSize / 2,
        selection.bottom() - squareSize / 2,
        squareSize, squareSize);

    painter.drawRect(
        selection.center().x() - squareSize / 2,
        selection.top() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.center().x() - squareSize / 2,
        selection.bottom() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.left() - squareSize / 2,
        selection.center().y() - squareSize / 2,
        squareSize, squareSize);
    painter.drawRect(
        selection.right() - squareSize / 2,
        selection.center().y() - squareSize / 2,
        squareSize, squareSize);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor("#ffffff"));

    const QRect bannerRect(
        selection.left(),
        qMax(imageRect.top(), selection.bottom() + 6),
        selection.width(),
        20);

    painter.drawText(
        bannerRect,
        Qt::AlignCenter,
        tr("Entrée : Valider | Échap : Annuler"));
}

void ArtworkCanvas::applyCrop()
{
    if (!m_cropSelectionRect.isValid() ||
        m_cropSelectionRect.width() < 15 ||
        m_cropSelectionRect.height() < 15) {
        return;
    }

    const QRect displayedImageRect = mosaicDisplayedRect();

    if (displayedImageRect.isEmpty())
        return;

    const double relativeX =
        static_cast<double>(
            m_cropSelectionRect.left() - displayedImageRect.left()) /
        displayedImageRect.width();

    const double relativeY =
        static_cast<double>(
            m_cropSelectionRect.top() - displayedImageRect.top()) /
        displayedImageRect.height();

    const double relativeWidth =
        static_cast<double>(m_cropSelectionRect.width()) /
        displayedImageRect.width();

    const double relativeHeight =
        static_cast<double>(m_cropSelectionRect.height()) /
        displayedImageRect.height();

    const double clampedX = qBound(0.0, relativeX, 1.0);
    const double clampedY = qBound(0.0, relativeY, 1.0);
    const double clampedWidth =
        qBound(0.0, relativeWidth, 1.0 - clampedX);
    const double clampedHeight =
        qBound(0.0, relativeHeight, 1.0 - clampedY);

    if (m_isCropped) {
        m_cropNormalizedRect = QRectF(
            m_cropNormalizedRect.x() +
                clampedX * m_cropNormalizedRect.width(),
            m_cropNormalizedRect.y() +
                clampedY * m_cropNormalizedRect.height(),
            clampedWidth * m_cropNormalizedRect.width(),
            clampedHeight * m_cropNormalizedRect.height());
    } else {
        m_cropNormalizedRect =
            QRectF(clampedX, clampedY, clampedWidth, clampedHeight);
    }

    const int previewWidth = m_mosaicPreview.width();
    const int previewHeight = m_mosaicPreview.height();

    const int pixelX = qBound(
        0,
        qRound(clampedX * previewWidth),
        previewWidth - 1);

    const int pixelY = qBound(
        0,
        qRound(clampedY * previewHeight),
        previewHeight - 1);

    const int pixelWidth = qBound(
        1,
        qRound(clampedWidth * previewWidth),
        previewWidth - pixelX);

    const int pixelHeight = qBound(
        1,
        qRound(clampedHeight * previewHeight),
        previewHeight - pixelY);

    m_mosaicPreview =
        m_mosaicPreview.copy(pixelX, pixelY, pixelWidth, pixelHeight);

    m_isCropped = true;
    m_cropSelectionRect = QRect();
    update();
}

void ArtworkCanvas::cancelCropSelection()
{
    m_cropSelectionRect = QRect();
    m_isCropping = false;
    update();
}

void ArtworkCanvas::resetCrop()
{
    if (!m_originalMosaicPreview.isNull()) {
        m_mosaicPreview = m_originalMosaicPreview;
        m_isCropped = false;
        m_cropNormalizedRect = QRectF(0, 0, 1, 1);
        m_cropSelectionRect = QRect();
        update();
    }
}