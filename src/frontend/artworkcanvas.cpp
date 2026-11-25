// src/frontend/artworkcanvas.cpp
#include "artworkcanvas.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <utility>

namespace {
constexpr int kMinimumWidth = 360;
constexpr int kMinimumHeight = 120;   // assez bas pour laisser grandir le panneau du bas ; le dessin est recadré
constexpr int kPairGutter = 22;
constexpr int kMinImageWidth = 130;
constexpr int kMinImageHeight = 180;
constexpr double kPairAspect = 0.78;
constexpr double kSingleAspect = 0.70;
constexpr int kMinZoomPercent = 5;
constexpr int kMaxZoomPercent = 1600;
constexpr double kZoomWheelFactor = 1.15;
constexpr qsizetype kImageCacheMaxBytes = 256 * 1024 * 1024; // 256 Mo max pour le cache d'aperçus

// Modes de l'espace de travail (voir FrontendUtils::MODE_NAMES).
constexpr int kModeGrid = 1;
constexpr int kModePair = 2;
constexpr int MODE_SEAM = 3;
constexpr int kModeExport = 4;   // Image finale

// Index de l'outil de recadrage dans la barre latérale gauche.
constexpr int kCropTool = 9;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_imageCache.setMaxCost(kImageCacheMaxBytes);
}

//enregistrement de la nouvelle paire choisie + envoie l'update pour afficher les bonnes paires à l'écran
void ArtworkCanvas::setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair)
{
    currentPair = pair;
    if (pair.pairIndex >= 0 && pair.pairIndex < m_allPairs.size()
        && m_allPairs.at(pair.pairIndex).pairIndex == pair.pairIndex) {
        m_allPairs[pair.pairIndex] = pair;
    }
    update();
}

// Vue d'ensemble de l'alignement (toutes les tuiles et leurs paires), ni paire isolée ni superposée.
bool ArtworkCanvas::isPairOverviewShown() const
{
    return modeValue == kModePair && !pairOverlayMode && !m_pairIsolated && !m_resultTiles.isEmpty();
}

QTransform ArtworkCanvas::viewTransform() const
{
    const qreal scale = zoomPercent / 100.0;
    const QPointF viewCenter(width() / 2.0, height() / 2.0);
    QTransform transform;
    transform.translate(viewCenter.x() + m_panOffset.x(),
        viewCenter.y() + m_panOffset.y());
    transform.scale(scale, scale);
    transform.translate(-viewCenter.x(), -viewCenter.y());
    return transform;
}

void ArtworkCanvas::resetView()
{
    zoomPercent = 100;
    m_panOffset = QPointF(0.0, 0.0);
    emit zoomChanged(100);
    update();
}

void ArtworkCanvas::setGridState(int rows, int columns,
    const QVector<GridTile>& tiles, int selectedCell)
{
    gridRows = rows;
    gridColumns = columns;
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
    return QRect((width() - cardW) / 2, (height() - cardH) / 2, cardW, cardH);
}

ArtworkCanvas::CropHandle ArtworkCanvas::hitTestCropHandle(const QPoint &pos) const
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.isEmpty()) {
        return CropHandle::None;
    }

    constexpr int margin = 12;
    const QRect &cropRect = m_cropSelectionRect;

    // Coins (prioritaires)
    if (QRect(cropRect.left() - margin, cropRect.top() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::TopLeft;
    if (QRect(cropRect.right() - margin, cropRect.top() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::TopRight;
    if (QRect(cropRect.left() - margin, cropRect.bottom() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::BottomLeft;
    if (QRect(cropRect.right() - margin, cropRect.bottom() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::BottomRight;

    // Bords
    if (qAbs(pos.y() - cropRect.top()) <= margin && pos.x() >= cropRect.left() && pos.x() <= cropRect.right())
        return CropHandle::Top;
    if (qAbs(pos.y() - cropRect.bottom()) <= margin && pos.x() >= cropRect.left() && pos.x() <= cropRect.right())
        return CropHandle::Bottom;
    if (qAbs(pos.x() - cropRect.left()) <= margin && pos.y() >= cropRect.top() && pos.y() <= cropRect.bottom())
        return CropHandle::Left;
    if (qAbs(pos.x() - cropRect.right()) <= margin && pos.y() >= cropRect.top() && pos.y() <= cropRect.bottom())
        return CropHandle::Right;

    // Déplacement complet à l'intérieur
    if (cropRect.contains(pos))
        return CropHandle::Move;

    return CropHandle::None;
}

void ArtworkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (!hasActiveImages()) {
        if (event->button() == Qt::LeftButton && emptyImportCardRect().contains(event->pos())) {
            emit importTilesRequested();
            event->accept();
            return;
        }
    }
    setFocus();
    if (modeValue == kModeGrid && event->button() == Qt::LeftButton) {
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

    if (modeValue == kModeExport && m_activeTool == kCropTool && event->button() == Qt::LeftButton) {
        const QPoint scenePos = viewTransform().inverted().map(event->pos());   // repère de l'image non zoomée
        const QRect displayedImageRect = mosaicDisplayedRect();
        if (displayedImageRect.contains(scenePos)) {
            m_cropDragStart = scenePos;
            m_cropRectAtDragStart = m_cropSelectionRect;
            m_activeHandle = hitTestCropHandle(scenePos);
            m_isCropping = true;

            // Si clic hors poignée et hors cadre, démarre une nouvelle sélection
            if (m_activeHandle == CropHandle::None) {
                m_cropStartPos = scenePos;
                m_cropSelectionRect = QRect(m_cropStartPos, QSize(0, 0));
            }
            update();
            event->accept();
            return;
        }
    }

    if (modeValue == kModePair && m_edit.isActive()) {
        const QPointF scenePos = viewTransform().inverted().map(QPointF(event->pos()));
        const auto [index, inA] = pickedPointAt(scenePos);
        // Clic droit : abandonne le point en attente, sinon supprime le point visé.
        if (event->button() == Qt::RightButton) {
            if (m_pendingPointInA.has_value()) {
                m_pendingPointInA.reset();
            } else if (index >= 0) {
                m_edit.remove(index);
            }
            emit manualPointsChanged();
            update();
            event->accept();
            return;
        }
        // Clic gauche sur un point : il est sélectionné et suit la souris jusqu'au relâchement.
        if (event->button() == Qt::LeftButton && index >= 0 && !m_pendingPointInA.has_value()) {
            m_edit.select(index);
            m_edit.beginMove();
            m_dragPointIndex = index;
            m_dragPointInA = inA;
            setCursor(Qt::SizeAllCursor);
            emit manualPointsChanged();
            update();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        m_pressPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ArtworkCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (modeValue == kModeExport && m_activeTool == kCropTool) {
        const QPoint scenePos = viewTransform().inverted().map(event->pos());   // repère de l'image non zoomée
        if (m_cropSelectionRect.isValid() && m_cropSelectionRect.contains(scenePos)) {
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
        const bool hovered = emptyImportCardRect().contains(event->pos());
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
    if (modeValue == kModePair && m_edit.isActive()) {
        m_hoverScenePos = viewTransform().inverted().map(QPointF(event->pos()));
        if (m_dragPointIndex >= 0) {
            dragPickedPoint(m_hoverScenePos);
            emit manualPointsChanged();
            update();
        } else {
            setCursor(pickedPointAt(m_hoverScenePos).first >= 0 ? Qt::PointingHandCursor : Qt::CrossCursor);
            if (m_pendingPointInA.has_value())
                update();
        }
        event->accept();
        return;
    }
    if (isPairOverviewShown()) {   // main au survol d'une paire : on peut la choisir d'un clic
        const bool overPair = pairAt(viewTransform().inverted().map(QPointF(event->pos()))) >= 0;
        if (overPair)
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();
        event->accept();
        return;
    }
    if (modeValue == kModeExport && m_activeTool == kCropTool) {
        const QPoint scenePos = viewTransform().inverted().map(event->pos());   // repère de l'image non zoomée
        const QRect displayedImageRect = mosaicDisplayedRect();
        const int clampedX = qBound(displayedImageRect.left(), scenePos.x(), displayedImageRect.right());
        const int clampedY = qBound(displayedImageRect.top(), scenePos.y(), displayedImageRect.bottom());
        const QPoint curPos(clampedX, clampedY);

        if (m_isCropping) {
            const QPoint delta = curPos - m_cropDragStart;
            if (m_activeHandle == CropHandle::None) {
                m_cropSelectionRect = QRect(m_cropStartPos, curPos).normalized();
            } else if (m_activeHandle == CropHandle::Move) {
                QRect moved = m_cropRectAtDragStart.translated(delta);
                if (moved.left() < displayedImageRect.left()) moved.moveLeft(displayedImageRect.left());
                if (moved.top() < displayedImageRect.top()) moved.moveTop(displayedImageRect.top());
                if (moved.right() > displayedImageRect.right()) moved.moveRight(displayedImageRect.right());
                if (moved.bottom() > displayedImageRect.bottom()) moved.moveBottom(displayedImageRect.bottom());
                m_cropSelectionRect = moved;
            } else {
                QRect adjustedCropRect = m_cropRectAtDragStart;
                switch (m_activeHandle) {
                    case CropHandle::TopLeft:
                        adjustedCropRect.setTop(qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                        adjustedCropRect.setLeft(qMin(curPos.x(), adjustedCropRect.right() - 15));
                        break;
                    case CropHandle::TopRight:
                        adjustedCropRect.setTop(qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                        adjustedCropRect.setRight(qMax(curPos.x(), adjustedCropRect.left() + 15));
                        break;
                    case CropHandle::BottomLeft:
                        adjustedCropRect.setBottom(qMax(curPos.y(), adjustedCropRect.top() + 15));
                        adjustedCropRect.setLeft(qMin(curPos.x(), adjustedCropRect.right() - 15));
                        break;
                    case CropHandle::BottomRight:
                        adjustedCropRect.setBottom(qMax(curPos.y(), adjustedCropRect.top() + 15));
                        adjustedCropRect.setRight(qMax(curPos.x(), adjustedCropRect.left() + 15));
                        break;
                    case CropHandle::Top:
                        adjustedCropRect.setTop(qMin(curPos.y(), adjustedCropRect.bottom() - 15));
                        break;
                    case CropHandle::Bottom:
                        adjustedCropRect.setBottom(qMax(curPos.y(), adjustedCropRect.top() + 15));
                        break;
                    case CropHandle::Left:
                        adjustedCropRect.setLeft(qMin(curPos.x(), adjustedCropRect.right() - 15));
                        break;
                    case CropHandle::Right:
                        adjustedCropRect.setRight(qMax(curPos.x(), adjustedCropRect.left() + 15));
                        break;
                    default:
                        break;
                }
                m_cropSelectionRect = adjustedCropRect.intersected(displayedImageRect);
            }
            update();
            event->accept();
            return;
        }

        // Survol : mise à jour du curseur selon la zone sous la souris
        if (m_cropSelectionRect.isValid() && !m_cropSelectionRect.isEmpty()) {
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
    if (modeValue == kModeGrid && event->button() == Qt::LeftButton) {
        const int targetCell = gridCellAt(event->pos());
        if (dragStartCell >= 0 && targetCell >= 0 && targetCell != dragStartCell) {
            if (cellsSwappedCallback)
                cellsSwappedCallback(dragStartCell, targetCell);
        }
        dragStartCell = -1;
        dragTargetCell = -1;
        unsetCursor();
        event->accept();
        return;
    }
    if (m_dragPointIndex >= 0) {
        m_dragPointIndex = -1;
        setCursor(Qt::CrossCursor);
        emit manualPointsChanged();
        event->accept();
        return;
    }
    if (m_panning) {
        m_panning = false;
        unsetCursor();
        // Clic sans glisser dans la vue Alignement : place un point manuel ou isole la paire cliquée.
        const bool isClick = (event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance();
        if (isClick && event->button() == Qt::LeftButton) {
            const QPointF scenePos = viewTransform().inverted().map(QPointF(event->pos()));
            if (modeValue == kModePair && m_edit.isActive()) {
                handlePointPickingClick(scenePos);
            } else if (isPairOverviewShown()) {
                const int pairIndex = pairAt(scenePos);
                if (pairIndex >= 0) {
                    setPairIsolated(true);
                    emit pairClicked(pairIndex);
                }
            }
        }
        event->accept();
        return;
    }

    if (modeValue == kModeExport && m_activeTool == kCropTool && event->button() == Qt::LeftButton && m_isCropping) {
        m_isCropping = false;
        m_activeHandle = CropHandle::None;
        if (m_cropSelectionRect.width() < 15 || m_cropSelectionRect.height() < 15) {
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

    const qreal factor = (delta > 0) ? kZoomWheelFactor : (1.0 / kZoomWheelFactor);
    const int newZoom = qBound(kMinZoomPercent, qRound(zoomPercent * factor), kMaxZoomPercent);
    if (newZoom != zoomPercent) {
        const qreal oldScale = zoomPercent / 100.0;
        const qreal newScale = newZoom / 100.0;
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF mousePos = event->position();
        m_panOffset = mousePos - center - (mousePos - center - m_panOffset) * (newScale / oldScale);
        zoomPercent = newZoom;
        emit zoomChanged(zoomPercent);
        update();
    }
    event->accept();
}

void ArtworkCanvas::keyPressEvent(QKeyEvent *event)
{
    if (modeValue == kModePair && m_edit.isActive() && handlePointPickingKey(event)) {
        event->accept();
        return;
    }
    if (modeValue == kModePair && !m_edit.isActive() && event->key() == Qt::Key_N
        && event->modifiers() == Qt::NoModifier) {
        emit nextPairToCheckRequested();
        event->accept();
        return;
    }
    if (modeValue == kModePair && event->key() == Qt::Key_Escape) {
        if (pairOverlayMode || m_pairIsolated) {
            setPairOverlayMode(false);
            setPairIsolated(false);
            event->accept();
            return;
        }
    }
    if (modeValue == kModeExport && m_activeTool == kCropTool) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
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
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal scale = zoomPercent / 100.0;
    const QPointF viewCenter(width() / 2.0, height() / 2.0);
    auto beginViewTransform = [&]() {
        p.save();
        p.translate(viewCenter + m_panOffset);
        p.scale(scale, scale);
        p.translate(-viewCenter);
    };

    if (modeValue == kModeExport) {
        // Calques superposés (onglet Calques), sauf si la mosaïque fusionnée est demandée ou pendant un recadrage,
        // qui s'applique toujours à la mosaïque fusionnée.
        const bool cropping = m_activeTool == kCropTool || m_isCropped;
        if (!fusedMosaicVisible && !cropping) {
            beginViewTransform();
            const bool layersDrawn = drawAssemblyLayers(p);
            p.restore();
            if (layersDrawn) {
                p.setPen(QColor("#dddddd"));
                p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                           QString::number(zoomPercent) + QStringLiteral(" %"));
                return;
            }
        }
        if (!m_mosaicPreview.isNull()) {
            const QRect displayedRect = mosaicDisplayedRect();
            beginViewTransform();
            p.drawImage(QRectF(displayedRect), m_mosaicPreview);
            // Mosaïque fusionnée demandée dans l'onglet Calques : la case « Lignes de couture » s'y applique aussi.
            // Pas sur une image recadrée : les coutures sont dans le repère de la mosaïque entière.
            if (fusedMosaicVisible && seamVisible && !cropping)
                drawSeamOverlay(p, QRectF(displayedRect));

            // Surcouche interactive de recadrage si l'outil est sélectionné
            if (m_activeTool == kCropTool) {
                drawCropOverlay(p, displayedRect);
            }
            p.restore();

            p.setPen(QColor("#dddddd"));
            p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                       QString::number(zoomPercent) + QStringLiteral(" %"));
            return;
        }

        // Si l'image n'est pas encore prête : message sobre centré
        QFont font = p.font();
        font.setPointSize(13);
        p.setFont(font);
        p.setPen(QColor("#b0b0b0"));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Un peu de patience : l'image n'est pas encore prête"));
        return;
    }

    // Ligne de couture : la mosaïque et ses coutures suivent le zoom, le message et le pourcentage non.
    if (modeValue == MODE_SEAM) {
        beginViewTransform();
        const bool drawn = drawMosaicPreview(p, true);
        p.restore();
        if (drawn) {
            p.setPen(QColor("#dddddd"));
            p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                       QString::number(zoomPercent) + QStringLiteral(" %"));
            return;
        }
    }
    if (!hasActiveImages()) {
        drawEmptyState(p);
        return;
    }

    if (modeValue == MODE_SEAM) {
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
    beginViewTransform();   // zoom et déplacement de la vue, aussi pour l'alignement
    if (pair) {
        drawPairView(p);
        p.restore();
        return;
    }

    if (!imagePaths.isEmpty())
        drawPhoto(p, firstImageRect, imagePaths.first());
    else
        drawPainting(p, firstImageRect);

    p.restore();
    p.setPen(QColor("#dddddd"));
    p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
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
        zoomPercent = 100;
        m_panOffset = QPointF(0.0, 0.0);
        emit zoomChanged(100);
        emit pairViewModeChanged();
        update();
    }
}

void ArtworkCanvas::setPairIsolated(bool isolated)
{
    if (m_pairIsolated != isolated) {
        m_pairIsolated = isolated;
        zoomPercent = 100;
        m_panOffset = QPointF(0.0, 0.0);
        emit zoomChanged(100);
        emit pairViewModeChanged();
        update();
    }
}

void ArtworkCanvas::startPointPicking(const QVector<QPointF> &initialPointsA,
                                      const QVector<QPointF> &initialPointsB)
{
    m_pendingPointInA.reset();
    m_dragPointIndex = -1;
    m_edit.start(initialPointsA, initialPointsB, defaultPairOffset());
    if (!pairOverlayMode)
        setPairIsolated(true);   // la vue choisie (côte à côte ou superposée) est conservée
    setFocus();
    emit manualPointsChanged();
    update();
}

void ArtworkCanvas::undoLastPickedPoint()
{
    if (m_pendingPointInA.has_value())
        m_pendingPointInA.reset();
    else
        m_edit.undo();
    emit manualPointsChanged();
    update();
}

void ArtworkCanvas::redoPickedPoint()
{
    m_edit.redo();
    emit manualPointsChanged();
    update();
}

void ArtworkCanvas::removeSelectedPickedPoint()
{
    m_edit.remove(m_edit.selected());
    emit manualPointsChanged();
    update();
}

void ArtworkCanvas::selectPickedPoint(int index)
{
    m_edit.select(index);
    update();
}

void ArtworkCanvas::stopPointPicking()
{
    if (!m_edit.isActive() && !m_pendingPointInA.has_value())
        return;
    m_edit.stop();
    m_pendingPointInA.reset();
    m_dragPointIndex = -1;
    unsetCursor();
    update();
}

void ArtworkCanvas::setPairOverlayOpacity(int percent)
{
    m_overlayOpacityPercent = qBound(0, percent, 100);
    update();
}

void ArtworkCanvas::setPairDifference(bool enabled)
{
    m_pairDifference = enabled;
    update();
}

// Raccourcis de la correction : Suppr, Ctrl+Z / Ctrl+Y, flèches (B de 1 px, Maj : 10 px), Entrée, Échap.
bool ArtworkCanvas::handlePointPickingKey(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Undo)) {
        undoLastPickedPoint();
        return true;
    }
    if (event->matches(QKeySequence::Redo)) {
        redoPickedPoint();
        return true;
    }
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    QPointF nudge;
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        removeSelectedPickedPoint();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit pointPickingApplyRequested();
        return true;
    case Qt::Key_Escape:
        if (m_pendingPointInA.has_value()) {
            m_pendingPointInA.reset();
            update();
        } else {
            stopPointPicking();
            emit pointPickingCancelled();
        }
        return true;
    case Qt::Key_Left: nudge = QPointF(-step, 0.0); break;
    case Qt::Key_Right: nudge = QPointF(step, 0.0); break;
    case Qt::Key_Up: nudge = QPointF(0.0, -step); break;
    case Qt::Key_Down: nudge = QPointF(0.0, step); break;
    default: return false;
    }
    m_edit.nudge(nudge);
    emit manualPointsChanged();
    update();
    return true;
}

void ArtworkCanvas::setMosaicPreview(const QImage &image) {
    m_mosaicPreview = image;
    m_originalMosaicPreview = image;
    m_isCropped = false;
    m_cropNormalizedRect = QRectF(0, 0, 1, 1);
    m_cropSelectionRect = QRect();
    update();
}

QRect ArtworkCanvas::mosaicDisplayedRect() const
{
    if (m_mosaicPreview.isNull()) return QRect();
    const QRect target = rect().adjusted(24, 24, -24, -24);
    const QSize scaledSize = m_mosaicPreview.size().scaled(target.size(), Qt::KeepAspectRatio);
    return QRect(target.left() + (target.width() - scaledSize.width()) / 2,
                 target.top() + (target.height() - scaledSize.height()) / 2,
                 scaledSize.width(), scaledSize.height());
}

void ArtworkCanvas::drawCropOverlay(QPainter &painter, const QRect &imageRect)
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.isEmpty()) {
        painter.setPen(QColor(240, 240, 240, 180));
        painter.drawText(QRect(imageRect.left(), imageRect.top() + 10, imageRect.width(), 24),
                         Qt::AlignCenter, tr("Cliquez et glissez sur l'image pour définir la zone à recadrer"));
        return;
    }

    // Assombrissement semi-transparent de la zone située hors du rectangle de recadrage.
    const QRegion fullRegion(imageRect);
    const QRegion cropRegion(m_cropSelectionRect);
    painter.setClipRegion(fullRegion.subtracted(cropRegion));
    painter.fillRect(imageRect, QColor(0, 0, 0, 150));
    painter.setClipping(false);

    // Cadre de découpe en pointillés blancs.
    painter.setPen(QPen(QColor("#ffffff"), 1, Qt::DashLine));
    painter.drawRect(m_cropSelectionRect);

    // Poignées d'angle accentuées en jaune doré (cornières).
    painter.setPen(QPen(QColor("#f0cf36"), 3));
    constexpr int handleLength = 12;
    const auto &selection = m_cropSelectionRect;
    // Coin supérieur gauche
    painter.drawLine(selection.left(), selection.top(), selection.left() + handleLength, selection.top());
    painter.drawLine(selection.left(), selection.top(), selection.left(), selection.top() + handleLength);
    // Coin supérieur droit
    painter.drawLine(selection.right(), selection.top(), selection.right() - handleLength, selection.top());
    painter.drawLine(selection.right(), selection.top(), selection.right(), selection.top() + handleLength);
    // Coin inférieur gauche
    painter.drawLine(selection.left(), selection.bottom(), selection.left() + handleLength, selection.bottom());
    painter.drawLine(selection.left(), selection.bottom(), selection.left(), selection.bottom() - handleLength);
    // Coin inférieur droit
    painter.drawLine(selection.right(), selection.bottom(), selection.right() - handleLength, selection.bottom());
    painter.drawLine(selection.right(), selection.bottom(), selection.right(), selection.bottom() - handleLength);

    // Poignées carrées préhensiles (4 coins + 4 milieux des côtés)
    constexpr int squareSize = 8;
    painter.setBrush(QColor("#f0cf36"));
    painter.setPen(QPen(QColor("#222222"), 1));
    // 4 coins
    painter.drawRect(selection.left() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.right() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.left() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.right() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    // 4 milieux de bordures
    painter.drawRect(selection.center().x() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.center().x() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.left() - squareSize / 2, selection.center().y() - squareSize / 2, squareSize, squareSize);
    painter.drawRect(selection.right() - squareSize / 2, selection.center().y() - squareSize / 2, squareSize, squareSize);
    painter.setBrush(Qt::NoBrush);

    // Guide contextuel rappelant les touches de raccourci.
    painter.setPen(QColor("#ffffff"));
    const QRect bannerRect(selection.left(), qMax(imageRect.top(), selection.bottom() + 6), selection.width(), 20);
    painter.drawText(bannerRect, Qt::AlignCenter, tr("Entrée : Valider | Échap : Annuler"));
}

void ArtworkCanvas::applyCrop()
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.width() < 15 || m_cropSelectionRect.height() < 15)
        return;

    const QRect displayedImageRect = mosaicDisplayedRect();
    if (displayedImageRect.isEmpty()) return;

    // Coordonnées relatives [0..1] par rapport à l'image actuellement affichée.
    const double relativeX = static_cast<double>(m_cropSelectionRect.left() - displayedImageRect.left()) / displayedImageRect.width();
    const double relativeY = static_cast<double>(m_cropSelectionRect.top() - displayedImageRect.top()) / displayedImageRect.height();
    const double relativeWidth = static_cast<double>(m_cropSelectionRect.width()) / displayedImageRect.width();
    const double relativeHeight = static_cast<double>(m_cropSelectionRect.height()) / displayedImageRect.height();

    const double clampedX = qBound(0.0, relativeX, 1.0);
    const double clampedY = qBound(0.0, relativeY, 1.0);
    const double clampedWidth = qBound(0.0, relativeWidth, 1.0 - clampedX);
    const double clampedHeight = qBound(0.0, relativeHeight, 1.0 - clampedY);

    if (m_isCropped) {
        m_cropNormalizedRect = QRectF(
            m_cropNormalizedRect.x() + clampedX * m_cropNormalizedRect.width(),
            m_cropNormalizedRect.y() + clampedY * m_cropNormalizedRect.height(),
            clampedWidth * m_cropNormalizedRect.width(),
            clampedHeight * m_cropNormalizedRect.height()
        );
    } else {
        m_cropNormalizedRect = QRectF(clampedX, clampedY, clampedWidth, clampedHeight);
    }

    // Découpe effective de l'aperçu affiché.
    const int previewWidth = m_mosaicPreview.width();
    const int previewHeight = m_mosaicPreview.height();
    const int pixelX = qBound(0, qRound(clampedX * previewWidth), previewWidth - 1);
    const int pixelY = qBound(0, qRound(clampedY * previewHeight), previewHeight - 1);
    const int pixelWidth = qBound(1, qRound(clampedWidth * previewWidth), previewWidth - pixelX);
    const int pixelHeight = qBound(1, qRound(clampedHeight * previewHeight), previewHeight - pixelY);

    m_mosaicPreview = m_mosaicPreview.copy(pixelX, pixelY, pixelWidth, pixelHeight);
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