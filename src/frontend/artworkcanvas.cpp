#include "artworkcanvas.h"

#include <QKeyEvent>
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

constexpr int TOOL_CROP = 9;
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
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

ArtworkCanvas::CropHandle ArtworkCanvas::hitTestCropHandle(const QPoint &pos) const
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.isEmpty()) {
        return CropHandle::None;
    }

    constexpr int margin = 12;
    const auto r = m_cropSelectionRect;

    // Coins (prioritaires)
    if (QRect(r.left() - margin, r.top() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::TopLeft;
    if (QRect(r.right() - margin, r.top() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::TopRight;
    if (QRect(r.left() - margin, r.bottom() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::BottomLeft;
    if (QRect(r.right() - margin, r.bottom() - margin, 2 * margin, 2 * margin).contains(pos))
        return CropHandle::BottomRight;

    // Bords
    if (qAbs(pos.y() - r.top()) <= margin && pos.x() >= r.left() && pos.x() <= r.right())
        return CropHandle::Top;
    if (qAbs(pos.y() - r.bottom()) <= margin && pos.x() >= r.left() && pos.x() <= r.right())
        return CropHandle::Bottom;
    if (qAbs(pos.x() - r.left()) <= margin && pos.y() >= r.top() && pos.y() <= r.bottom())
        return CropHandle::Left;
    if (qAbs(pos.x() - r.right()) <= margin && pos.y() >= r.top() && pos.y() <= r.bottom())
        return CropHandle::Right;

    // Déplacement complet à l'intérieur
    if (r.contains(pos))
        return CropHandle::Move;

    return CropHandle::None;
}

void ArtworkCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus();
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

    if (modeValue == kModeExport && m_activeTool == TOOL_CROP && event->button() == Qt::LeftButton) {
        const QRect imgRect = mosaicDisplayedRect();
        if (imgRect.contains(event->pos())) {
            m_cropDragStart = event->pos();
            m_cropRectAtDragStart = m_cropSelectionRect;
            m_activeHandle = hitTestCropHandle(event->pos());
            m_isCropping = true;

            // Si clic hors poignée et hors cadre, démarre une nouvelle sélection
            if (m_activeHandle == CropHandle::None) {
                m_cropStartPos = event->pos();
                m_cropSelectionRect = QRect(m_cropStartPos, QSize(0, 0));
            }
            update();
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void ArtworkCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (modeValue == kModeExport && m_activeTool == TOOL_CROP) {
        if (m_cropSelectionRect.isValid() && m_cropSelectionRect.contains(event->pos())) {
            applyCrop();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ArtworkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (modeValue == kModeExport && m_activeTool == TOOL_CROP) {
        const QRect imgRect = mosaicDisplayedRect();
        const int clampedX = qBound(imgRect.left(), event->pos().x(), imgRect.right());
        const int clampedY = qBound(imgRect.top(), event->pos().y(), imgRect.bottom());
        const QPoint curPos(clampedX, clampedY);

        if (m_isCropping) {
            const QPoint delta = curPos - m_cropDragStart;
            if (m_activeHandle == CropHandle::None) {
                m_cropSelectionRect = QRect(m_cropStartPos, curPos).normalized();
            } else if (m_activeHandle == CropHandle::Move) {
                QRect moved = m_cropRectAtDragStart.translated(delta);
                if (moved.left() < imgRect.left()) moved.moveLeft(imgRect.left());
                if (moved.top() < imgRect.top()) moved.moveTop(imgRect.top());
                if (moved.right() > imgRect.right()) moved.moveRight(imgRect.right());
                if (moved.bottom() > imgRect.bottom()) moved.moveBottom(imgRect.bottom());
                m_cropSelectionRect = moved;
            } else {
                QRect r = m_cropRectAtDragStart;
                switch (m_activeHandle) {
                    case CropHandle::TopLeft:
                        r.setTop(qMin(curPos.y(), r.bottom() - 15));
                        r.setLeft(qMin(curPos.x(), r.right() - 15));
                        break;
                    case CropHandle::TopRight:
                        r.setTop(qMin(curPos.y(), r.bottom() - 15));
                        r.setRight(qMax(curPos.x(), r.left() + 15));
                        break;
                    case CropHandle::BottomLeft:
                        r.setBottom(qMax(curPos.y(), r.top() + 15));
                        r.setLeft(qMin(curPos.x(), r.right() - 15));
                        break;
                    case CropHandle::BottomRight:
                        r.setBottom(qMax(curPos.y(), r.top() + 15));
                        r.setRight(qMax(curPos.x(), r.left() + 15));
                        break;
                    case CropHandle::Top:
                        r.setTop(qMin(curPos.y(), r.bottom() - 15));
                        break;
                    case CropHandle::Bottom:
                        r.setBottom(qMax(curPos.y(), r.top() + 15));
                        break;
                    case CropHandle::Left:
                        r.setLeft(qMin(curPos.x(), r.right() - 15));
                        break;
                    case CropHandle::Right:
                        r.setRight(qMax(curPos.x(), r.left() + 15));
                        break;
                    default:
                        break;
                }
                m_cropSelectionRect = r.intersected(imgRect);
            }
            update();
            event->accept();
            return;
        }

        // Survol : mise à jour du curseur selon la zone sous la souris
        if (m_cropSelectionRect.isValid() && !m_cropSelectionRect.isEmpty()) {
            const CropHandle handle = hitTestCropHandle(event->pos());
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

    if (modeValue == kModeExport && m_activeTool == TOOL_CROP && event->button() == Qt::LeftButton && m_isCropping) {
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

void ArtworkCanvas::keyPressEvent(QKeyEvent *event)
{
    if (modeValue == kModeExport && m_activeTool == TOOL_CROP) {
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

    if (modeValue == kModeExport) {
        if (!m_mosaicPreview.isNull()) {
            const QRect displayedRect = mosaicDisplayedRect();
            const QImage fitted = m_mosaicPreview.scaled(displayedRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(displayedRect.topLeft(), fitted);

            // Surcouche interactive de recadrage si l'outil est sélectionné
            if (m_activeTool == TOOL_CROP) {
                drawCropOverlay(p, displayedRect);
            }

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

    if (!imagePaths.isEmpty())
        drawPhoto(p, firstImageRect, imagePaths.at(indexA));
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
                   tr("Masque d'exclusion — proposition"));
    } else if (modeValue == kModeAssembly) {
        if(seamVisible){
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
        }
        if (comparison) {
            p.setPen(QPen(QColor("#f0cf36"), 2));
            p.drawLine(x0 + imageW / 2, y0, x0 + imageW / 2, y0 + imageH);
            p.fillRect(QRect(x0, y0, imageW / 2, imageH), QColor(225, 188, 40, 30));
        }
        p.setPen(QPen(QColor("#e4ca37"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(x0, y0, imageW, imageH));
        if (alignmentErrorVisible) {
            const QRect errorZoneRect(x0 + imageW * .58, y0, imageW * .20, imageH);
            p.fillRect(errorZoneRect, QColor(228, 202, 55, 35));
            p.drawRect(errorZoneRect);
            p.setPen(QColor("#f0cf36"));
            p.drawText(QRect(errorZoneRect.left() - 20, y0 - 22, errorZoneRect.width() + 80, 20),
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

void ArtworkCanvas::drawCropOverlay(QPainter &p, const QRect &imageRect)
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.isEmpty()) {
        p.setPen(QColor(240, 240, 240, 180));
        p.drawText(QRect(imageRect.left(), imageRect.top() + 10, imageRect.width(), 24),
                   Qt::AlignCenter, tr("Cliquez et glissez sur l'image pour définir la zone à recadrer"));
        return;
    }

    // Assombrissement semi-transparent de la zone située hors du rectangle de recadrage.
    const QRegion fullRegion(imageRect);
    const QRegion cropRegion(m_cropSelectionRect);
    p.setClipRegion(fullRegion.subtracted(cropRegion));
    p.fillRect(imageRect, QColor(0, 0, 0, 150));
    p.setClipping(false);

    // Cadre de découpe en pointillés blancs.
    p.setPen(QPen(QColor("#ffffff"), 1, Qt::DashLine));
    p.drawRect(m_cropSelectionRect);

    // Poignées d'angle accentuées en jaune doré (cornières).
    p.setPen(QPen(QColor("#f0cf36"), 3));
    constexpr int handleLength = 12;
    const auto &selection = m_cropSelectionRect;
    // Coin supérieur gauche
    p.drawLine(selection.left(), selection.top(), selection.left() + handleLength, selection.top());
    p.drawLine(selection.left(), selection.top(), selection.left(), selection.top() + handleLength);
    // Coin supérieur droit
    p.drawLine(selection.right(), selection.top(), selection.right() - handleLength, selection.top());
    p.drawLine(selection.right(), selection.top(), selection.right(), selection.top() + handleLength);
    // Coin inférieur gauche
    p.drawLine(selection.left(), selection.bottom(), selection.left() + handleLength, selection.bottom());
    p.drawLine(selection.left(), selection.bottom(), selection.left(), selection.bottom() - handleLength);
    // Coin inférieur droit
    p.drawLine(selection.right(), selection.bottom(), selection.right() - handleLength, selection.bottom());
    p.drawLine(selection.right(), selection.bottom(), selection.right(), selection.bottom() - handleLength);

    // Poignées carrées préhensiles (4 coins + 4 milieux des côtés)
    constexpr int squareSize = 8;
    p.setBrush(QColor("#f0cf36"));
    p.setPen(QPen(QColor("#222222"), 1));
    // 4 coins
    p.drawRect(selection.left() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.right() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.left() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.right() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    // 4 milieux de bordures
    p.drawRect(selection.center().x() - squareSize / 2, selection.top() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.center().x() - squareSize / 2, selection.bottom() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.left() - squareSize / 2, selection.center().y() - squareSize / 2, squareSize, squareSize);
    p.drawRect(selection.right() - squareSize / 2, selection.center().y() - squareSize / 2, squareSize, squareSize);
    p.setBrush(Qt::NoBrush);

    // Guide contextuel rappelant les touches de raccourci.
    p.setPen(QColor("#ffffff"));
    const QRect bannerRect(selection.left(), qMax(imageRect.top(), selection.bottom() + 6), selection.width(), 20);
    p.drawText(bannerRect, Qt::AlignCenter, tr("Entrée : Valider | Échap : Annuler"));
}

void ArtworkCanvas::applyCrop()
{
    if (!m_cropSelectionRect.isValid() || m_cropSelectionRect.width() < 15 || m_cropSelectionRect.height() < 15)
        return;

    const QRect imgRect = mosaicDisplayedRect();
    if (imgRect.isEmpty()) return;

    // Coordonnées relatives [0..1] par rapport à l'image actuellement affichée.
    const double relX = double(m_cropSelectionRect.left() - imgRect.left()) / imgRect.width();
    const double relY = double(m_cropSelectionRect.top() - imgRect.top()) / imgRect.height();
    const double relW = double(m_cropSelectionRect.width()) / imgRect.width();
    const double relH = double(m_cropSelectionRect.height()) / imgRect.height();

    const double clampedX = qBound(0.0, relX, 1.0);
    const double clampedY = qBound(0.0, relY, 1.0);
    const double clampedW = qBound(0.0, relW, 1.0 - clampedX);
    const double clampedH = qBound(0.0, relH, 1.0 - clampedY);

    if (m_isCropped) {
        m_cropNormalizedRect = QRectF(
            m_cropNormalizedRect.x() + clampedX * m_cropNormalizedRect.width(),
            m_cropNormalizedRect.y() + clampedY * m_cropNormalizedRect.height(),
            clampedW * m_cropNormalizedRect.width(),
            clampedH * m_cropNormalizedRect.height()
        );
    } else {
        m_cropNormalizedRect = QRectF(clampedX, clampedY, clampedW, clampedH);
    }

    // Découpe effective de l'aperçu affiché.
    const int maxW = m_mosaicPreview.width();
    const int maxH = m_mosaicPreview.height();
    const int pixelX = qBound(0, qRound(clampedX * maxW), maxW - 1);
    const int pixelY = qBound(0, qRound(clampedY * maxH), maxH - 1);
    const int pixelW = qBound(1, qRound(clampedW * maxW), maxW - pixelX);
    const int pixelH = qBound(1, qRound(clampedH * maxH), maxH - pixelY);

    m_mosaicPreview = m_mosaicPreview.copy(pixelX, pixelY, pixelW, pixelH);
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