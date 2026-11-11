#include "artworkcanvas.h"
#include "frontendutils.h"

#include "core/io/pyramidal_tiff_reader.h"

#include <QDebug>
#include <QFutureWatcher>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

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
constexpr double kMaximumZoom = 1600.0;
constexpr double kMinimumFitZoom = 0.01;

QImage imageForDisplay(const cv::Mat &pixels)
{
    if (pixels.empty())
        return {};

    cv::Mat eightBit;
    if (pixels.depth() == CV_16U)
        pixels.convertTo(eightBit, CV_MAKETYPE(CV_8U, pixels.channels()), 1.0 / 257.0);
    else
        eightBit = pixels;

    QImage::Format format = QImage::Format_Invalid;
    switch (eightBit.channels()) {
    case 1:
        format = QImage::Format_Grayscale8;
        break;
    case 3:
        format = QImage::Format_RGB888;
        break;
    case 4:
        format = QImage::Format_RGBA8888;
        break;
    default:
        return {};
    }

    return QImage(eightBit.data, eightBit.cols, eightBit.rows,
                  static_cast<int>(eightBit.step), format).copy();
}
}

ArtworkCanvas::ArtworkCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setMouseTracking(true);
    mosaicRequestTimer = new QTimer(this);
    mosaicRequestTimer->setSingleShot(true);
    mosaicRequestTimer->setInterval(35);
    connect(mosaicRequestTimer, &QTimer::timeout,
            this, &ArtworkCanvas::startMosaicRegionRead);
    const auto defaultPairs = FrontendUtils::defaultPairAlignments();
    if (!defaultPairs.isEmpty()) {
        currentPair = defaultPairs.first();
    }
}

ArtworkCanvas::~ArtworkCanvas()
{
    if (mosaicRequestTimer)
        mosaicRequestTimer->stop();
    if (activeMosaicRead) {
        activeMosaicRead->disconnect();
        activeMosaicRead->waitForFinished();
    }
}

void ArtworkCanvas::clearMosaic()
{
    mosaicTiffPath.clear();
    mosaicSize = {};
    mosaicOrigin = {};
    mosaicRegionImage = {};
    mosaicRegionInFullResolution = {};
    mosaicRegionLevel = -1;
    mosaicReadError.clear();
    m_mosaicPreview = {};
    mosaicReadPending = false;
    ++mosaicRequestId;
    if (mosaicRequestTimer)
        mosaicRequestTimer->stop();
    update();
}

bool ArtworkCanvas::setMosaicTiff(const QString &path, QString *errorMessage)
{
    if (path.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("Aucun fichier TIFF sélectionné.");
        return false;
    }

    IONs::PyramidalTiffReaderClass reader(std::filesystem::path(path.toStdWString()));
    std::vector<cv::Size> dimensions;
    std::string error;
    if (!reader.readPyramidDimensions(dimensions, error)) {
        if (errorMessage)
            *errorMessage = QString::fromStdString(error);
        return false;
    }
    if (dimensions.empty()) {
        if (errorMessage)
            *errorMessage = tr("Le TIFF ne contient aucun niveau d’image.");
        return false;
    }

    mosaicTiffPath = path;
    mosaicSize = QSize(dimensions.front().width, dimensions.front().height);
    mosaicOrigin = {};
    mosaicRegionImage = {};
    mosaicRegionInFullResolution = {};
    mosaicRegionLevel = -1;
    mosaicReadError.clear();
    m_mosaicPreview = {};
    mosaicReadPending = false;
    ++mosaicRequestId;
    fitMosaicToView();
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void ArtworkCanvas::setMosaicPreview(const QImage &image)
{
    mosaicTiffPath.clear();
    mosaicSize = {};
    mosaicRegionImage = {};
    mosaicRegionInFullResolution = {};
    mosaicRegionLevel = -1;
    mosaicReadError.clear();
    mosaicReadPending = false;
    ++mosaicRequestId;
    if (mosaicRequestTimer)
        mosaicRequestTimer->stop();
    m_mosaicPreview = image;
    update();
}

void ArtworkCanvas::setZoomChangedCallback(std::function<void(double)> callback)
{
    zoomChangedCallback = std::move(callback);
}

void ArtworkCanvas::updateZoom(double zoom, bool notify)
{
    zoomPercent = zoom;
    if (notify && zoomChangedCallback)
        zoomChangedCallback(zoomPercent);
}

void ArtworkCanvas::setZoom(double zoom)
{
    const double boundedZoom = std::clamp(zoom, kMinimumFitZoom, kMaximumZoom);
    if (mosaicTiffPath.isEmpty()) {
        mosaicScale = boundedZoom / 100.0;
        updateZoom(boundedZoom, true);
        update();
        return;
    }
    applyMosaicZoom(boundedZoom, QRectF(rect()).center());
}

void ArtworkCanvas::fitMosaicToView()
{
    if (mosaicTiffPath.isEmpty() || mosaicSize.isEmpty()) {
        updateZoom(100.0, true);
        mosaicScale = 1.0;
        update();
        return;
    }

    const QSize contentSize = size();
    const double scale = qMin(
        static_cast<double>(contentSize.width()) / mosaicSize.width(),
        static_cast<double>(contentSize.height()) / mosaicSize.height());
    mosaicScale = std::clamp(scale, kMinimumFitZoom / 100.0, kMaximumZoom / 100.0);
    updateZoom(mosaicScale * 100.0, true);
    mosaicOrigin = {};
    requestMosaicRegion();
    update();
}

void ArtworkCanvas::zoomIn()
{
    constexpr double ZOOM_THRESHOLD = 5.0;
    constexpr double ZOOM_FACTOR = 1.25;
    constexpr double ZOOM_STEP = 5.0;
    constexpr double ZOOM_INCREMENT = 0.25;
    const double currentZoom = zoomPercent;
    double newZoom = 0.0;
    if (currentZoom < ZOOM_THRESHOLD) {
        newZoom = currentZoom * ZOOM_FACTOR;
        if (newZoom <= currentZoom)
            newZoom = currentZoom + ZOOM_INCREMENT;
    } else {
        newZoom = currentZoom + ZOOM_STEP;
    }
    setZoom(std::min(kMaximumZoom, newZoom));
}

void ArtworkCanvas::zoomOut()
{
    constexpr double ZOOM_THRESHOLD = 5.0;
    constexpr double ZOOM_FACTOR = 1.25;
    constexpr double ZOOM_STEP = 5.0;
    const double currentZoom = zoomPercent;
    double newZoom = 0.0;
    if (currentZoom > ZOOM_THRESHOLD) {
        newZoom = currentZoom - ZOOM_STEP;
        if (newZoom < 1.0)
            newZoom = std::max(kMinimumFitZoom, currentZoom / ZOOM_FACTOR);
    } else {
        newZoom = currentZoom / ZOOM_FACTOR;
    }
    setZoom(std::max(kMinimumFitZoom, newZoom));
}

QRectF ArtworkCanvas::mosaicImageRect() const
{
    if (mosaicSize.isEmpty())
        return {};

    const double width = mosaicSize.width() * mosaicScale;
    const double height = mosaicSize.height() * mosaicScale;
    const double left = width <= this->width()
        ? (this->width() - width) / 2.0
        : -mosaicOrigin.x() * mosaicScale;
    const double top = height <= this->height()
        ? (this->height() - height) / 2.0
        : -mosaicOrigin.y() * mosaicScale;
    return QRectF(left, top, width, height);
}

QPointF ArtworkCanvas::clampedMosaicOrigin(const QPointF &origin, double scale) const
{
    const double visibleWidth = width() / scale;
    const double visibleHeight = height() / scale;
    return QPointF(
        std::clamp(origin.x(), 0.0, qMax(0.0, mosaicSize.width() - visibleWidth)),
        std::clamp(origin.y(), 0.0, qMax(0.0, mosaicSize.height() - visibleHeight)));
}

void ArtworkCanvas::applyMosaicZoom(double zoomPercent, const QPointF &anchor)
{
    const QRectF oldImage = mosaicImageRect();
    const QPointF sourceAnchor(
        (anchor.x() - oldImage.left()) / mosaicScale,
        (anchor.y() - oldImage.top()) / mosaicScale);
    mosaicScale = std::clamp(zoomPercent / 100.0,
                             kMinimumFitZoom / 100.0, kMaximumZoom / 100.0);
    updateZoom(mosaicScale * 100.0, true);

    const double imageWidth = mosaicSize.width() * mosaicScale;
    const double imageHeight = mosaicSize.height() * mosaicScale;
    const double imageLeft = imageWidth <= width()
        ? (width() - imageWidth) / 2.0
        : anchor.x() - sourceAnchor.x() * mosaicScale;
    const double imageTop = imageHeight <= height()
        ? (height() - imageHeight) / 2.0
        : anchor.y() - sourceAnchor.y() * mosaicScale;
    mosaicOrigin = clampedMosaicOrigin(
        QPointF(-imageLeft / mosaicScale, -imageTop / mosaicScale), mosaicScale);
    requestMosaicRegion();
    update();
}

void ArtworkCanvas::requestMosaicRegion()
{
    if (mosaicTiffPath.isEmpty() || mosaicSize.isEmpty())
        return;
    ++mosaicRequestId;
    mosaicReadError.clear();
    mosaicRequestTimer->start();
    update();
}

void ArtworkCanvas::startMosaicRegionRead()
{
    if (mosaicTiffPath.isEmpty() || mosaicSize.isEmpty())
        return;
    if (activeMosaicRead) {
        mosaicReadPending = true;
        return;
    }

    const quint64 requestId = mosaicRequestId;
    const QString path = mosaicTiffPath;
    const QPointF origin = mosaicOrigin;
    const QSize viewportSize = size();
    const double zoomPercentValue = mosaicScale * 100.0;
    qInfo().noquote() << "Mosaic viewport request: zoom" << zoomPercentValue
                      << "%, origin" << origin.x() << origin.y()
                      << ", viewport" << viewportSize.width() << "x"
                      << viewportSize.height();

    auto *watcher = new QFutureWatcher<MosaicRegionResult>(this);
    activeMosaicRead = watcher;
    connect(watcher, &QFutureWatcher<MosaicRegionResult>::finished, this,
            [this, watcher, requestId]() {
        acceptMosaicRegion(requestId, watcher->result());
        if (activeMosaicRead == watcher)
            activeMosaicRead = nullptr;
        watcher->deleteLater();
        if (mosaicReadPending && !mosaicTiffPath.isEmpty()) {
            mosaicReadPending = false;
            startMosaicRegionRead();
        }
    });
    watcher->setFuture(QtConcurrent::run([path, origin, viewportSize, zoomPercentValue]() {
        MosaicRegionResult result;
        try {
            IONs::PyramidalTiffReaderClass reader(std::filesystem::path(path.toStdWString()));
            IONs::PyramidViewportRegion viewport;
            std::string error;
            if (!reader.readViewport(
                    IONs::PyramidViewportRequest{
                        cv::Point2d(origin.x(), origin.y()),
                        cv::Size(viewportSize.width(), viewportSize.height()),
                        zoomPercentValue},
                    viewport, error)) {
                result.errorMessage = QString::fromStdString(error);
                return result;
            }

            result.image = imageForDisplay(viewport.pixels);
            result.fullResolutionRegion = QRectF(
                viewport.fullResolutionRegion.x,
                viewport.fullResolutionRegion.y,
                viewport.fullResolutionRegion.width,
                viewport.fullResolutionRegion.height);
            result.level = viewport.level;
            qInfo().noquote() << "Mosaic TIFF read: level" << viewport.level
                              << "region" << viewport.region.x << viewport.region.y
                              << viewport.region.width << viewport.region.height;
            if (result.image.isNull())
                result.errorMessage = QObject::tr(
                    "Le format de pixels du TIFF n’est pas pris en charge.");
        } catch (const std::exception &exception) {
            result.errorMessage = QString::fromUtf8(exception.what());
        }
        return result;
    }));
}

void ArtworkCanvas::acceptMosaicRegion(
    quint64 requestId, const MosaicRegionResult &result)
{
    if (requestId != mosaicRequestId)
        return;
    if (!result.errorMessage.isEmpty()) {
        mosaicReadError = result.errorMessage;
        qWarning().noquote() << "Mosaic viewport read failed:" << result.errorMessage;
        update();
        return;
    }

    mosaicReadError.clear();
    mosaicRegionImage = result.image;
    mosaicRegionInFullResolution = result.fullResolutionRegion;
    mosaicRegionLevel = result.level;
    update();
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
    if (modeValue == kModeExport && !mosaicTiffPath.isEmpty() &&
        event->button() == Qt::LeftButton) {
        panningMosaic = true;
        lastPanPosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

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

void ArtworkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (panningMosaic) {
        const QPoint delta = event->pos() - lastPanPosition;
        lastPanPosition = event->pos();
        mosaicOrigin = clampedMosaicOrigin(
            mosaicOrigin - QPointF(delta.x() / mosaicScale, delta.y() / mosaicScale),
            mosaicScale);
        requestMosaicRegion();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ArtworkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (panningMosaic && event->button() == Qt::LeftButton) {
        panningMosaic = false;
        unsetCursor();
        event->accept();
        return;
    }

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

void ArtworkCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mosaicTiffPath.isEmpty() || mosaicSize.isEmpty())
        return;
    mosaicOrigin = clampedMosaicOrigin(mosaicOrigin, mosaicScale);
    requestMosaicRegion();
}

void ArtworkCanvas::wheelEvent(QWheelEvent *event)
{
    if (modeValue != kModeExport || mosaicTiffPath.isEmpty()) {
        QWidget::wheelEvent(event);
        return;
    }

    const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
    applyMosaicZoom(mosaicScale * 100.0 * factor, event->position());
    event->accept();
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
        p.drawImage(target, m_mosaicPreview.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        // On affiche le pourcentage de zoom en bas à gauche même pour la mosaïque
        p.setPen(QColor("#dddddd"));
        p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                   FrontendUtils::formatZoom(zoomPercent));
        return; // On arrête de dessiner ici
    }

    if (modeValue == kModeExport && !mosaicTiffPath.isEmpty()) {
        const QRectF imageRect = mosaicImageRect();
        p.save();
        p.setClipRect(rect());
        if (!mosaicReadError.isEmpty()) {
            p.setPen(QColor("#ff8b82"));
            p.drawText(rect().adjusted(24, 24, -24, -24),
                       Qt::AlignCenter | Qt::TextWordWrap, mosaicReadError);
        } else if (!mosaicRegionImage.isNull()) {
            const QRectF target(
                imageRect.left() + mosaicRegionInFullResolution.x() * mosaicScale,
                imageRect.top() + mosaicRegionInFullResolution.y() * mosaicScale,
                mosaicRegionInFullResolution.width() * mosaicScale,
                mosaicRegionInFullResolution.height() * mosaicScale);
            p.drawImage(target, mosaicRegionImage);
        } else {
            p.setPen(QColor("#dddddd"));
            p.drawText(rect(), Qt::AlignCenter, tr("Chargement de la région visible…"));
        }
        p.restore();

        p.setPen(QColor("#dddddd"));
        const QString zoomStr = FrontendUtils::formatZoom(zoomPercent);
        const QString zoomDetails = mosaicRegionLevel >= 0
            ? QStringLiteral("%1 — niveau %2").arg(zoomStr).arg(mosaicRegionLevel)
            : QStringLiteral("%1 — chargement…").arg(zoomStr);
        p.drawText(QRect(14, height() - 30, 220, 20), Qt::AlignLeft, zoomDetails);
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
    } else if (modeValue == kModeAssembly || modeValue == kModeExport) {
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
               FrontendUtils::formatZoom(zoomPercent));
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
