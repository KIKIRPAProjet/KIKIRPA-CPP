#include "mainwindow.h"
#include "logpanel.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QFrame>
#include <QTransform>
#include <QPixmap>
#include <QtMath>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <wincodec.h>
#include <windows.h>
#endif

namespace {
const QStringList modeNames = {
    QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
    QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Export")
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("primary", primary);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}

QVector<int> captureOrderCells(int rows, int columns, int order)
{
    QVector<int> cells;
    cells.reserve(rows * columns);
    switch (order) {
    case 1:
        for (int column = 0; column < columns; ++column) {
            const bool bottomUp = (column % 2 == 0);
            for (int offset = 0; offset < rows; ++offset) {
                const int row = bottomUp ? rows - 1 - offset : offset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 2:
        for (int offset = 0; offset < columns; ++offset) {
            const int column = columns - 1 - offset;
            const bool topDown = (offset % 2 == 0);
            for (int rowOffset = 0; rowOffset < rows; ++rowOffset) {
                const int row = topDown ? rowOffset : rows - 1 - rowOffset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 3:
        for (int row = 0; row < rows; ++row) {
            for (int offset = 0; offset < columns; ++offset) {
                const int column = row % 2 == 0 ? offset : columns - 1 - offset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 4:
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                cells.append(row * columns + column);
        break;
    case 5:
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < rows; ++row)
                cells.append(row * columns + column);
        break;
    default:
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column)
                cells.append(row * columns + column);
        }
        break;
    }
    return cells;
}

#ifdef Q_OS_WIN
QImage readWindowsTiff(const QString &path, const QSize &maximumSize)
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
        return {};

    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICFormatConverter *converter = nullptr;
    QImage image;

    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&factory)))) {
            break;
        }
        if (FAILED(factory->CreateDecoderFromFilename(
                reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder))) {
            break;
        }
        if (FAILED(decoder->GetFrame(0, &frame)))
            break;

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        if (FAILED(frame->GetSize(&sourceWidth, &sourceHeight)) ||
            sourceWidth == 0 || sourceHeight == 0)
            break;

        const double scale = qMin(static_cast<double>(maximumSize.width()) / sourceWidth,
                                  static_cast<double>(maximumSize.height()) / sourceHeight);
        const UINT previewWidth = qMax<UINT>(1, qRound(sourceWidth * qMin(1.0, scale)));
        const UINT previewHeight = qMax<UINT>(1, qRound(sourceHeight * qMin(1.0, scale)));
        IWICBitmapSource *source = frame;
        if (previewWidth != sourceWidth || previewHeight != sourceHeight) {
            if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(frame, previewWidth, previewHeight,
                                          WICBitmapInterpolationModeFant))) {
                break;
            }
            source = scaler;
        }

        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) {
            break;
        }

        UINT width = 0;
        UINT height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
            break;

        image = QImage(static_cast<int>(width), static_cast<int>(height), QImage::Format_ARGB32);
        if (image.isNull() ||
            FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(image.bytesPerLine()),
                                         static_cast<UINT>(image.sizeInBytes()),
                                         image.bits()))) {
            image = {};
        }
    } while (false);

    if (converter)
        converter->Release();
    if (scaler)
        scaler->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (factory)
        factory->Release();
    if (uninitialize)
        CoUninitialize();
    return image;
}
#endif

QImage readPreviewImage(const QString &path, const QSize &maximumSize)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const double scale = qMin(static_cast<double>(maximumSize.width()) / sourceSize.width(),
                                  static_cast<double>(maximumSize.height()) / sourceSize.height());
        if (scale > 0.0 && scale < 1.0)
            reader.setScaledSize(QSize(qMax(1, qRound(sourceSize.width() * scale)),
                                       qMax(1, qRound(sourceSize.height() * scale))));
    }
    QImage image = reader.read();
#ifdef Q_OS_WIN
    if (image.isNull())
        image = readWindowsTiff(path, maximumSize);
#endif
    if (!image.isNull())
        image = image.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

class ArtworkCanvas final : public QWidget
{
public:
    explicit ArtworkCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 260);
        setMouseTracking(true);
    }

    void setMode(int mode) { m_mode = mode; update(); }
    void setZoom(int zoom) { m_zoom = zoom; update(); }
    void setImagePaths(const QStringList &paths) { m_imagePaths = paths; update(); }
    void setGridState(int rows, int columns, int order, const QVector<GridTile> &tiles,
                      int selectedCell)
    {
        m_gridRows = rows;
        m_gridColumns = columns;
        m_captureOrder = order;
        m_gridTiles = tiles;
        m_selectedGridCell = selectedCell;
        update();
    }
    void setGridCallbacks(std::function<void(int)> selectionChanged,
                          std::function<void(int, int)> cellsSwapped)
    {
        m_selectionChanged = std::move(selectionChanged);
        m_cellsSwapped = std::move(cellsSwapped);
    }
    void toggleComparison() { m_comparison = !m_comparison; update(); }
    void toggleMask() { m_mask = !m_mask; update(); }
    void setMaskVisible(bool visible) { m_mask = visible; update(); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_mode == 1 && event->button() == Qt::LeftButton) {
            m_dragStartCell = gridCellAt(event->pos());
            if (m_dragStartCell >= 0) {
                m_selectedGridCell = m_dragStartCell;
                if (m_selectionChanged)
                    m_selectionChanged(m_selectedGridCell);
                update();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_mode == 1 && event->button() == Qt::LeftButton) {
            const int targetCell = gridCellAt(event->pos());
            if (m_dragStartCell >= 0 && targetCell >= 0 && targetCell != m_dragStartCell) {
                if (m_cellsSwapped)
                    m_cellsSwapped(m_dragStartCell, targetCell);
            }
            m_dragStartCell = -1;
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

        if (m_mode == 1) {
            drawGrid(p);
            return;
        }

        const bool pair = m_mode == 3;
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
        if (!m_imagePaths.isEmpty())
            drawPhoto(p, firstImageRect, m_imagePaths.first());
        else
            drawPainting(p, firstImageRect);
        if (pair) {
            const QRect secondImageRect(x0 + imageW + gutter, y0, imageW, imageH);
            if (m_imagePaths.size() > 1)
                drawPhoto(p, secondImageRect, m_imagePaths.at(1));
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

        if (m_mode == 2 && m_mask) {
            QPainterPath mask;
            mask.moveTo(x0 + imageW * 0.10, y0);
            mask.lineTo(x0 + imageW * 0.98, y0);
            mask.lineTo(x0 + imageW * 0.98, y0 + imageH * 0.06);
            mask.lineTo(x0 + imageW * 0.12, y0 + imageH * 0.10);
            mask.closeSubpath();
            p.fillPath(mask, QColor(185, 39, 36, 150));
            p.setPen(QPen(QColor("#ef5149"), 2));
            p.drawPath(mask);
            p.setPen(QColor("#eeeeee"));
            p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft,
                       tr("Masque d'exclusion — proposition"));
        } else if (m_mode == 4 || m_mode == 5) {
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
            if (m_comparison) {
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
                   QString::number(m_zoom) + QStringLiteral(" %"));
    }

private:
    void drawGrid(QPainter &p)
    {
        const int rows = qMax(1, m_gridRows);
        const int columns = qMax(1, m_gridColumns);
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
        m_gridCellRects.clear();
        m_gridCellRects.reserve(cellCount);

        const QVector<int> orderCells = captureOrderCells(rows, columns, m_captureOrder);
        QVector<int> captureNumbers(cellCount, 0);
        QVector<QPointF> routePoints;
        int sequence = 0;
        for (const int cellIndex : orderCells) {
            if (cellIndex >= m_gridTiles.size())
                continue;
            const GridTile &tile = m_gridTiles.at(cellIndex);
            if (tile.path.isEmpty() || tile.empty)
                continue;
            captureNumbers[cellIndex] = ++sequence;
            const int row = cellIndex / columns;
            const int column = cellIndex % columns;
            routePoints.append(QPointF(left + column * (cellW + gapX) + cellW / 2.0,
                                       top + row * (cellH + gapY) + cellH / 2.0));
        }

        int activeTileCount = 0;
        for (int index = 0; index < qMin(cellCount, static_cast<int>(m_gridTiles.size())); ++index) {
            const GridTile &tile = m_gridTiles.at(index);
            if (!tile.path.isEmpty() && !tile.empty)
                ++activeTileCount;
        }

        for (int index = 0; index < cellCount; ++index) {
            const int row = index / columns;
            const int column = index % columns;
            const QRect cell(left + column * (cellW + gapX),
                             top + row * (cellH + gapY), cellW, cellH);
            m_gridCellRects.append(cell);
            const GridTile tile = index < m_gridTiles.size() ? m_gridTiles.at(index) : GridTile{};

            if (tile.path.isEmpty() && !tile.empty) {
                if (index == m_selectedGridCell) {
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

            p.setPen(QPen(index == m_selectedGridCell ? QColor("#4b9bea") : QColor("#e8c638"),
                          index == m_selectedGridCell ? 3 : 1, Qt::DashLine));
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
        for (int index = 0; index < m_gridCellRects.size(); ++index)
            if (m_gridCellRects.at(index).contains(point))
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

    int m_mode = 4;
    int m_zoom = 12;
    QStringList m_imagePaths;
    QVector<GridTile> m_gridTiles;
    QVector<QRect> m_gridCellRects;
    std::function<void(int)> m_selectionChanged;
    std::function<void(int, int)> m_cellsSwapped;
    int m_gridRows = 3;
    int m_gridColumns = 2;
    int m_captureOrder = 0;
    int m_selectedGridCell = 0;
    int m_dragStartCell = -1;
    bool m_mask = true;
    bool m_comparison = false;
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("KIK-IRPA Assemblage"));
    resize(1228, 780);
    setMinimumSize(920, 600);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget { background:#303030; color:#d5d5d5; font-size:12px; }"
        "QMenuBar,QMenu { background:#242424; color:#dedede; }"
        "QMenuBar::item:selected,QMenu::item:selected { background:#3d6e9d; }"
        "QToolBar { background:#2c2c2c; border:0; spacing:4px; }"
        "QPushButton,QToolButton,QComboBox,QSpinBox,QLineEdit { background:#3b3b3b;"
        " color:#dedede; border:1px solid #4a4a4a; border-radius:3px; padding:5px 8px; }"
        "QPushButton:hover,QToolButton:hover { background:#4a4a4a; }"
        "QPushButton[primary=\"true\"] { background:#397fb8; border-color:#438bca; color:white; }"
        "QPushButton[primary=\"true\"]:hover { background:#4b91cd; }"
        "QPushButton:checked { background:#292929; border-color:#353535; color:#999; }"
        "QTabWidget::pane { border:1px solid #424242; }"
        "QTabBar::tab { background:#303030; color:#aaa; padding:7px 12px; }"
        "QTabBar::tab:selected { background:#3b3b3b; color:white; border-bottom:2px solid #438bca; }"
        "QTableWidget,QListWidget { background:#303030; alternate-background-color:#363636;"
        " gridline-color:#484848; selection-background-color:#36536d; }"
        "QHeaderView::section { background:#303030; color:#bdbdbd; border:0;"
        " border-bottom:1px solid #505050; padding:5px; }"
        "QProgressBar { background:#242424; border:0; text-align:center; height:8px; }"
        "QProgressBar::chunk { background:#43a56c; }"
        "QSplitter::handle { background:#202020; }"
        "QSplitter::handle:hover { background:#438bca; }"
        "QLabel[muted=\"true\"] { color:#999; }"
        "QCheckBox { spacing:7px; }"));

    pages = new QStackedWidget(this);
    setCentralWidget(pages);
    logPanel = new LogPanel(this);
    addDockWidget(Qt::BottomDockWidgetArea, logPanel);
    logPanel->hide();
    createHomePage();
    createWorkspace();

    auto *fileMenu = menuBar()->addMenu(tr("Fichier"));
    auto *newAction = fileMenu->addAction(tr("Nouveau projet…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::createProject);
    auto *openAction = fileMenu->addAction(tr("Ouvrir un projet…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    auto *importAction = fileMenu->addAction(tr("Importer un dossier de photos…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MainWindow::importTiles);
    auto *saveAction = fileMenu->addAction(tr("Enregistrer le projet"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    fileMenu->addSeparator();
    auto *exportAction = fileMenu->addAction(tr("Exporter / rapport…"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::showExportDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Retour à l’accueil"), this, [this]() {
        pages->setCurrentIndex(0);
    });
    fileMenu->addAction(tr("Quitter"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu(tr("Édition"));
    editMenu->addAction(tr("Annuler"), QKeySequence::Undo, this,
                        [this]() { updateStatus(tr("Dernière action annulée.")); });
    editMenu->addAction(tr("Rétablir"), QKeySequence::Redo, this,
                        [this]() { updateStatus(tr("Action rétablie.")); });
    auto *viewMenu = menuBar()->addMenu(tr("Affichage"));
    auto *togglePanels = viewMenu->addAction(tr("Afficher / masquer les panneaux"));
    connect(togglePanels, &QAction::triggered, this, [this]() {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
    });
    auto *zoomIn = viewMenu->addAction(tr("Zoom avant"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *zoomOut = viewMenu->addAction(tr("Zoom arrière"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *fit = viewMenu->addAction(tr("Ajuster à la fenêtre"));
    fit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fit, &QAction::triggered, this, [this]() {
        zoomPercent = 100;
        zoomLabel->setText(QStringLiteral("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *windowMenu = menuBar()->addMenu(tr("Fenêtre"));
    auto *toggleLogAction = logPanel->toggleViewAction();
    toggleLogAction->setText(tr("Afficher le journal"));
    windowMenu->addAction(toggleLogAction);
    auto *toolsMenu = menuBar()->addMenu(tr("Tuiles"));
    toolsMenu->addAction(tr("Importer un dossier de photos…"), this, &MainWindow::importTiles);
    toolsMenu->addAction(tr("Organiser la grille"), this, [this]() {
        setMode(1);
        updateStatus(tr("Grille réorganisée selon l'ordre sélectionné."));
    });
    auto *masksMenu = menuBar()->addMenu(tr("Masques"));
    masksMenu->addAction(tr("Détecter les bords noirs"), this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Proposition de masque affichée — à valider par l'opérateur."));
    });
    auto *assemblyMenu = menuBar()->addMenu(tr("Assemblage"));
    assemblyMenu->addAction(tr("Tout lancer"), QKeySequence(Qt::Key_F5), this,
                            [this]() { startRun(1); });
    assemblyMenu->addAction(tr("Relancer à partir de…"), this, [this]() { startRun(6); });
    assemblyMenu->addAction(tr("Comparer deux exécutions…"), this, &MainWindow::showComparison);
    auto *helpMenu = menuBar()->addMenu(tr("Aide"));
    helpMenu->addAction(tr("À propos"), this, [this]() {
        QMessageBox::about(this, tr("À propos"),
            tr("KIK-IRPA Assemblage — prototype d'interface.\n"
               "Les traitements d'image nécessitent le backend, qui n'est pas encore connecté."));
    });

    connect(pages, &QStackedWidget::currentChanged, this, [this](int index) {
        menuBar()->setVisible(index != 0);
    });
    menuBar()->setVisible(false);
}

void MainWindow::createHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *side = new QWidget;
    side->setFixedWidth(245);
    side->setStyleSheet(QStringLiteral("background:#202020;"));
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    auto *brand = new QLabel(tr("KIK-IRPA Assemblage"));
    brand->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;color:#d5d5d5;"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(14);
    sideLayout->addWidget(mutedLabel(tr("Assemblage de tuiles haute résolution\n"
                                        "de peintures planes."), side));
    sideLayout->addSpacing(16);
    auto *newProject = makeButton(tr("+ Nouveau projet…"), side, true);
    sideLayout->addWidget(newProject);
    auto *open = makeButton(tr("Importer un projet…"), side);
    sideLayout->addWidget(open);
    sideLayout->addStretch();
    sideLayout->addWidget(mutedLabel(tr("Manuel d'utilisation · Raccourcis clavier · À propos"), side));
    layout->addWidget(side);

    auto *recentArea = new QWidget;
    auto *recentLayout = new QVBoxLayout(recentArea);
    recentLayout->setContentsMargins(30, 26, 30, 24);
    recentLayout->addWidget(new QLabel(tr("Projets récents")));
    recentEmptyLabel = mutedLabel(tr("Aucun projet récent."), recentArea);
    recentLayout->addWidget(recentEmptyLabel);
    auto *cardsContainer = new QWidget(recentArea);
    recentCardsLayout = new QHBoxLayout(cardsContainer);
    recentCardsLayout->setContentsMargins(0, 0, 0, 0);
    recentCardsLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    recentLayout->addWidget(cardsContainer);
    recentLayout->addStretch();
    layout->addWidget(recentArea, 1);
    pages->addWidget(page);
    refreshRecentProjects();
    connect(newProject, &QPushButton::clicked, this, &MainWindow::createProject);
    connect(open, &QPushButton::clicked, this, &MainWindow::openProject);
}

void MainWindow::refreshRecentProjects()
{
    if (!recentCardsLayout || !recentEmptyLabel)
        return;

    while (QLayoutItem *item = recentCardsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    QStringList validPaths;
    for (const QString &path : recentPaths) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(absolutePath) && !validPaths.contains(absolutePath))
            validPaths.append(absolutePath);
    }
    if (validPaths != recentPaths) {
        recentPaths = validPaths;
        settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    }

    const auto formatModifiedDate = [](const QDateTime &dateTime) {
        return dateTime.isValid()
            ? dateTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))
            : QObject::tr("Date inconnue");
    };

    int cardCount = 0;
    if (projectCreated && projectPath.isEmpty()) {
        auto *card = makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(projectName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le : %2")
                                       .arg(tilePaths.size())
                                       .arg(tr("Non enregistré")), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this]() {
            pages->setCurrentWidget(workspace);
            setMode(1);
            updateStatus(tr("Projet courant : %1").arg(projectName));
        });
        ++cardCount;
    }

    for (const QString &path : recentPaths) {
        QFile file(path);
        QString recentName = QFileInfo(path).completeBaseName();
        int recentTileCount = 0;
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) {
                const QJsonObject project = document.object();
                recentName = project.value(QStringLiteral("project_name"))
                                 .toString(recentName);
                const QJsonArray paths = project.value(QStringLiteral("tiles")).toArray();
                recentTileCount = paths.size();
            }
        }

        auto *card = makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(recentName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        name->setWordWrap(true);
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le %2")
                                       .arg(recentTileCount)
                                       .arg(formatModifiedDate(QFileInfo(path).lastModified())), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this, path]() {
            loadProjectFromPath(path);
        });
        ++cardCount;
    }

    recentCardsLayout->addStretch();
    recentEmptyLabel->setVisible(cardCount == 0);
}

void MainWindow::rememberRecentProject(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo::exists(absolutePath))
        return;

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    recentPaths.removeAll(absolutePath);
    recentPaths.prepend(absolutePath);
    while (recentPaths.size() > 10)
        recentPaths.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    refreshRecentProjects();
}

void MainWindow::createProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nouveau projet"),
        tr("Nom du projet :"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    const QString selectedTileDirectory = QFileDialog::getExistingDirectory(
        this, tr("Choisir le dossier des tuiles"));
    if (selectedTileDirectory.isEmpty())
        return;

    const QFileInfoList tileFiles = QDir(selectedTileDirectory).entryInfoList(
        {QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
        QDir::Files, QDir::Name);

    projectName = name;
    tileDirectory = selectedTileDirectory;
    outputDirectory.clear();
    projectPath.clear();
    projectCreated = true;
    tilePaths.clear();
    for (const QFileInfo &tileFile : tileFiles)
        tilePaths.append(tileFile.absoluteFilePath());

    gridRows = 3;
    gridColumns = 2;
    gridRows = qMax(1, qMin(20, (static_cast<int>(tilePaths.size()) + gridColumns - 1)
                                     / gridColumns));
    captureOrder = 0;
    horizontalOverlap = 20;
    verticalOverlap = 20;
    overlapsLinked = true;
    modality = QStringLiteral("Infrarouge");
    scaleMmPerPixel = 0.0;
    uncertaintyMargin = 0.0;
    automaticUncertainty = true;
    referenceImagePath.clear();
    selectedGridCell = 0;
    gridValidated = false;
    initializeGridFromPaths();

    pages->setCurrentWidget(workspace);
    setMode(0);
    refreshProjectInfo();
    if (tilePaths.isEmpty()) {
        updateStatus(tr("Projet créé. Aucun fichier image reconnu dans le dossier des tuiles."));
    } else {
        updateStatus(tr("Projet créé avec %1 tuile(s) trouvée(s).").arg(tilePaths.size()));
    }
    refreshRecentProjects();
}

void MainWindow::createWorkspace()
{
    workspace = new QWidget;
    auto *root = new QVBoxLayout(workspace);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *toolbar = new QWidget;
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);
    auto *title = new QLabel(tr("VanDerVeken — Infrarouge — KIK-IRPA Assemblage"));
    title->setObjectName(QStringLiteral("workspaceTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();
    auto *save = makeButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveProject);
    root->addWidget(toolbar);

    auto *stages = new QWidget;
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);
    for (int i = 0; i < modeNames.size(); ++i) {
        auto *button = makeButton(QStringLiteral("%1   %2").arg(i + 1).arg(modeNames.at(i)), stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { setMode(i); });
    }
    auto *run = makeButton(tr("▶ Tout lancer"), stages, true);
    stageLayout->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() { startRun(1); });
    auto *compare = makeButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked, this, &MainWindow::showComparison);
    root->addWidget(stages);

    auto *modeOptions = new QWidget;
    auto *optionsLayout = new QHBoxLayout(modeOptions);
    optionsLayout->setContentsMargins(10, 3, 10, 3);
    auto *modeLabel = new QLabel(tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"));
    modeLabel->setObjectName(QStringLiteral("modeDescription"));
    optionsLayout->addWidget(modeLabel);
    optionsLayout->addStretch();
    arrangementCombo = new QComboBox;
    arrangementCombo->addItems({tr("Automatique (par lignes)"),
                                tr("Serpentin vertical, bas gauche"),
                                tr("Serpentin vertical, haut droite"),
                                tr("Serpentin horizontal"), tr("Par lignes"), tr("Par colonnes")});
    arrangementCombo->setObjectName(QStringLiteral("arrangementCombo"));
    optionsLayout->addWidget(arrangementCombo);
    connect(arrangementCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int value) {
        captureOrder = value;
        gridValidated = false;
        refreshGrid();
        updateStatus(tr("Ordre de prise de vue : %1").arg(arrangementCombo->currentText()));
    });
    root->addWidget(modeOptions);

    auto *body = new QSplitter(Qt::Horizontal);
    body->setHandleWidth(5);
    body->setChildrenCollapsible(false);
    auto *tools = new QWidget;
    tools->setFixedWidth(42);
    auto *toolLayout = new QVBoxLayout(tools);
    toolLayout->setContentsMargins(4, 5, 4, 5);
    const QStringList toolNames = {tr("Main (H)"), tr("Zoom (Z)"), tr("Sélection (V)"),
        tr("Pinceau (B)"), tr("Gomme (E)"), tr("Lasso (L)"), tr("Rectangle (M)"),
        tr("Point (P)"), tr("Mesure (I)")};
    for (int i = 0; i < toolNames.size(); ++i) {
        auto *tool = new QToolButton(tools);
        tool->setText(QStringList{QStringLiteral("✋"), QStringLiteral("⌕"), QStringLiteral("↖"),
                                  QStringLiteral("✎"), QStringLiteral("⌫"), QStringLiteral("⬠"),
                                  QStringLiteral("▭"), QStringLiteral("＋"), QStringLiteral("⌁")}.at(i));
        tool->setToolTip(toolNames.at(i));
        tool->setFixedSize(32, 32);
        toolLayout->addWidget(tool);
        connect(tool, &QToolButton::clicked, this, [this, toolNames, i]() {
            updateStatus(tr("Outil actif : %1").arg(toolNames.at(i)));
            if (currentMode == 2 && i >= 3 && i <= 6) {
                static_cast<ArtworkCanvas *>(canvas)->toggleMask();
            }
        });
    }
    toolLayout->addStretch();
    body->addWidget(tools);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    projectInfoPanel = new QWidget(center);
    auto *projectInfoLayout = new QVBoxLayout(projectInfoPanel);
    projectInfoLayout->setContentsMargins(32, 32, 32, 32);
    projectInfoLayout->setSpacing(14);
    auto *projectInfoTitle = new QLabel(tr("Informations du projet"), projectInfoPanel);
    projectInfoTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
    projectInfoLayout->addWidget(projectInfoTitle);

    auto *projectNameForm = new QFormLayout;
    projectNameEdit = new QLineEdit(projectInfoPanel);
    projectNameEdit->setObjectName(QStringLiteral("projectNameEdit"));
    auto *renameProject = makeButton(tr("Renommer"), projectInfoPanel);
    auto *projectNameRow = new QHBoxLayout;
    projectNameRow->addWidget(projectNameEdit, 1);
    projectNameRow->addWidget(renameProject);
    projectNameForm->addRow(tr("Nom du projet"), projectNameRow);
    connect(renameProject, &QPushButton::clicked, this, [this]() {
        const QString newName = projectNameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, tr("Nom invalide"),
                tr("Le nom du projet ne peut pas être vide."));
            return;
        }
        projectName = newName;
        refreshProjectInfo();
        updateStatus(tr("Projet renommé : %1").arg(projectName));
    });
    projectInfoLayout->addLayout(projectNameForm);

    projectFolderLabel = mutedLabel(QString(), projectInfoPanel);
    projectFolderLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFolderLabel);
    projectFormatLabel = new QLabel(projectInfoPanel);
    projectFormatLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFormatLabel);
    projectInfoLayout->addStretch();
    centerLayout->addWidget(projectInfoPanel, 1);

    canvas = new ArtworkCanvas(center);
    centerLayout->addWidget(canvas, 1);
    auto *canvasControls = new QWidget;
    canvasControls->setObjectName(QStringLiteral("canvasControls"));
    auto *canvasControlsLayout = new QHBoxLayout(canvasControls);
    canvasControlsLayout->setContentsMargins(8, 3, 8, 3);
    auto *minus = makeButton(QStringLiteral("−"), canvasControls);
    auto *plus = makeButton(QStringLiteral("+"), canvasControls);
    zoomLabel = new QLabel(QStringLiteral("12,4 %"));
    auto *fit = makeButton(tr("Ajuster"), canvasControls);
    auto *oneToOne = makeButton(QStringLiteral("100 %"), canvasControls);
    canvasControlsLayout->addWidget(minus);
    canvasControlsLayout->addWidget(plus);
    canvasControlsLayout->addWidget(zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    centerLayout->addWidget(canvasControls);
    projectInfoPanel->setVisible(false);
    connect(minus, &QPushButton::clicked, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 5);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    connect(plus, &QPushButton::clicked, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 5);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    connect(fit, &QPushButton::clicked, this, [this]() {
        zoomLabel->setText(tr("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(100);
    });
    connect(oneToOne, &QPushButton::clicked, this, [this]() {
        zoomPercent = 100;
        zoomLabel->setText(QStringLiteral("100 %"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    body->addWidget(center);

    rightTabs = new QTabWidget;
    rightTabs->setMinimumWidth(218);

    auto *gridScroll = new QScrollArea;
    gridScroll->setWidgetResizable(true);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridTabPage = new QWidget;
    auto *gridLayout = new QVBoxLayout(gridTabPage);
    gridLayout->setContentsMargins(7, 8, 7, 8);
    gridSummaryLabel = new QLabel;
    gridLayout->addWidget(gridSummaryLabel);

    auto *dimensions = new QFormLayout;
    gridRowsSpin = new QSpinBox(gridTabPage);
    gridRowsSpin->setRange(1, 20);
    gridRowsSpin->setValue(gridRows);
    gridRowsSpin->setObjectName(QStringLiteral("gridRowsSpin"));
    gridColumnsSpin = new QSpinBox(gridTabPage);
    gridColumnsSpin->setRange(1, 20);
    gridColumnsSpin->setValue(gridColumns);
    gridColumnsSpin->setObjectName(QStringLiteral("gridColumnsSpin"));
    dimensions->addRow(tr("Lignes"), gridRowsSpin);
    dimensions->addRow(tr("Colonnes"), gridColumnsSpin);
    gridLayout->addLayout(dimensions);

    auto *orderGroup = new QGroupBox(tr("Ordre de prise de vue"), gridTabPage);
    auto *orderLayout = new QVBoxLayout(orderGroup);
    captureOrderCombo = new QComboBox(orderGroup);
    captureOrderCombo->addItems({
        tr("Automatique (par lignes)"),
        tr("Serpentin vertical, bas gauche"),
        tr("Serpentin vertical, haut droite"),
        tr("Serpentin horizontal"),
        tr("Par lignes"),
        tr("Par colonnes")
    });
    orderLayout->addWidget(captureOrderCombo);
    gridLayout->addWidget(orderGroup);

    selectedGridTileLabel = mutedLabel(QString(), gridTabPage);
    selectedGridTileLabel->setWordWrap(true);
    gridLayout->addWidget(selectedGridTileLabel);
    auto *tileActions = new QHBoxLayout;
    auto *rotateTile = makeButton(tr("Pivoter 90°"), gridTabPage);
    auto *flipTile = makeButton(tr("Retourner"), gridTabPage);
    tileActions->addWidget(rotateTile);
    tileActions->addWidget(flipTile);
    gridLayout->addLayout(tileActions);
    emptyCellButton = makeButton(tr("Marquer une case vide"), gridTabPage);
    gridLayout->addWidget(emptyCellButton);

    auto *captureGroup = new QGroupBox(tr("Prise de vue"), gridTabPage);
    auto *captureForm = new QFormLayout(captureGroup);
    horizontalOverlapSpin = new QSpinBox(captureGroup);
    horizontalOverlapSpin->setRange(0, 90);
    horizontalOverlapSpin->setSuffix(QStringLiteral(" %"));
    verticalOverlapSpin = new QSpinBox(captureGroup);
    verticalOverlapSpin->setRange(0, 90);
    verticalOverlapSpin->setSuffix(QStringLiteral(" %"));
    horizontalOverlapSpin->setValue(horizontalOverlap);
    verticalOverlapSpin->setValue(verticalOverlap);
    linkedOverlapCheck = new QCheckBox(tr("Lier les deux valeurs"), captureGroup);
    linkedOverlapCheck->setChecked(overlapsLinked);
    captureForm->addRow(tr("Recouvrement horizontal"), horizontalOverlapSpin);
    captureForm->addRow(tr("Recouvrement vertical"), verticalOverlapSpin);
    captureForm->addRow(QString(), linkedOverlapCheck);
    modalityCombo = new QComboBox(captureGroup);
    modalityCombo->addItems({tr("Infrarouge"), tr("Lumière visible"), tr("Ultraviolet"),
                             tr("Radiographie"), tr("Autre")});
    captureForm->addRow(tr("Modalité"), modalityCombo);
    scaleSpin = new QDoubleSpinBox(captureGroup);
    scaleSpin->setRange(0.0, 1000000.0);
    scaleSpin->setDecimals(6);
    scaleSpin->setSingleStep(0.01);
    scaleSpin->setSpecialValueText(tr("À renseigner"));
    scaleSpin->setSuffix(tr(" mm / pixel"));
    captureForm->addRow(tr("Échelle"), scaleSpin);
    automaticUncertaintyCheck = new QCheckBox(tr("Automatique"), captureGroup);
    automaticUncertaintyCheck->setChecked(automaticUncertainty);
    uncertaintySpin = new QDoubleSpinBox(captureGroup);
    uncertaintySpin->setRange(0.0, 1000000.0);
    uncertaintySpin->setDecimals(3);
    uncertaintySpin->setSuffix(tr(" px"));
    uncertaintySpin->setEnabled(!automaticUncertainty);
    captureForm->addRow(tr("Marge d'incertitude"), uncertaintySpin);
    captureForm->addRow(QString(), automaticUncertaintyCheck);
    gridLayout->addWidget(captureGroup);

    referencePathLabel = mutedLabel(tr("Aucune image de référence"), gridTabPage);
    referencePathLabel->setWordWrap(true);
    gridLayout->addWidget(referencePathLabel);
    auto *importReference = makeButton(tr("Importer une image de référence…"), gridTabPage);
    auto *compareReference = makeButton(tr("Comparer à l'image de référence"), gridTabPage);
    gridLayout->addWidget(importReference);
    gridLayout->addWidget(compareReference);
    auto *validateGridButton = makeButton(tr("Valider la grille ✓"), gridTabPage, true);
    gridLayout->addWidget(validateGridButton);
    gridLayout->addStretch();
    gridScroll->setWidget(gridTabPage);
    gridScroll->setObjectName(QStringLiteral("gridTabScrollArea"));
    gridTabPage = gridScroll;

    connect(gridRowsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        gridRows = value;
        selectedGridCell = qMin(selectedGridCell, gridRows * gridColumns - 1);
        gridValidated = false;
        refreshGrid();
    });
    connect(gridColumnsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        gridColumns = value;
        selectedGridCell = qMin(selectedGridCell, gridRows * gridColumns - 1);
        gridValidated = false;
        refreshGrid();
    });
    connect(captureOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int value) {
        captureOrder = value;
        gridValidated = false;
        refreshGrid();
    });
    connect(horizontalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        horizontalOverlap = value;
        if (overlapsLinked && verticalOverlapSpin)
            verticalOverlapSpin->setValue(value);
        gridValidated = false;
        refreshGrid();
    });
    connect(verticalOverlapSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        verticalOverlap = value;
        if (overlapsLinked && horizontalOverlapSpin)
            horizontalOverlapSpin->setValue(value);
        gridValidated = false;
        refreshGrid();
    });
    connect(linkedOverlapCheck, &QCheckBox::toggled, this, [this](bool linked) {
        overlapsLinked = linked;
        if (linked && verticalOverlapSpin)
            verticalOverlapSpin->setValue(horizontalOverlap);
        gridValidated = false;
        refreshGrid();
    });
    connect(modalityCombo, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        modality = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(scaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        scaleMmPerPixel = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(automaticUncertaintyCheck, &QCheckBox::toggled, this, [this](bool automatic) {
        automaticUncertainty = automatic;
        uncertaintySpin->setEnabled(!automatic);
        gridValidated = false;
        refreshGridControls();
    });
    connect(uncertaintySpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        uncertaintyMargin = value;
        gridValidated = false;
        refreshGridControls();
    });
    connect(rotateTile, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
            gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
            updateStatus(tr("Sélectionnez d'abord une tuile non vide."));
            return;
        }
        gridTiles[selectedGridCell].rotation = (gridTiles.at(selectedGridCell).rotation + 90) % 360;
        gridValidated = false;
        refreshGrid();
    });
    connect(flipTile, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
            gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
            updateStatus(tr("Sélectionnez d'abord une tuile non vide."));
            return;
        }
        gridTiles[selectedGridCell].flipped = !gridTiles.at(selectedGridCell).flipped;
        gridValidated = false;
        refreshGrid();
    });
    connect(emptyCellButton, &QPushButton::clicked, this, [this]() {
        if (selectedGridCell < 0 || selectedGridCell >= gridRows * gridColumns) {
            updateStatus(tr("Sélectionnez une case de la grille."));
            return;
        }
        while (gridTiles.size() <= selectedGridCell)
            gridTiles.append(GridTile{});
        gridTiles[selectedGridCell].empty = !gridTiles.at(selectedGridCell).empty;
        gridValidated = false;
        refreshGrid();
    });
    connect(importReference, &QPushButton::clicked, this, &MainWindow::importReferenceImage);
    connect(compareReference, &QPushButton::clicked, this, &MainWindow::compareReferenceImage);
    connect(validateGridButton, &QPushButton::clicked, this, &MainWindow::validateGrid);
    static_cast<ArtworkCanvas *>(canvas)->setGridCallbacks(
        [this](int cell) {
            selectedGridCell = cell;
            refreshGridControls();
        },
        [this](int first, int second) { swapGridTiles(first, second); });

    auto *layersPage = new QWidget;
    auto *layersLayout = new QVBoxLayout(layersPage);
    layersLayout->addWidget(new QLabel(tr("Calques")));
    const QStringList layerNames = {tr("Lignes de couture"), tr("Erreur d'alignement"),
                                    tr("Tuiles"), tr("Mosaïque"), tr("Masques")};
    for (const QString &name : layerNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        layersLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, name](bool checked) {
            updateStatus(tr("Calque « %1 » %2.").arg(name, checked ? tr("affiché") : tr("masqué")));
            if (name == tr("Masques")) {
                maskVisible = checked;
                static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(checked);
            }
        });
    }
    layersLayout->addStretch();
    rightTabs->addTab(layersPage, tr("Calques"));

    auto *chainPage = new QWidget;
    auto *chainLayout = new QVBoxLayout(chainPage);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));
    const QStringList steps = {tr("1  Import"), tr("2  Masques"), tr("3  Correspondance"),
        tr("4  Optimisation"), tr("5  Calage local"), tr("6  Égalisation"),
        tr("7  Ligne de couture"), tr("8  Fusion et export")};
    auto *stepList = new QListWidget(chainPage);
    for (int i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem((i == 2 ? QStringLiteral("⚠  ") : QStringLiteral("●  "))
                                         + steps.at(i), stepList);
        item->setData(Qt::UserRole, i + 1);
    }
    chainLayout->addWidget(stepList);
    connect(stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        startRun(item->data(Qt::UserRole).toInt());
    });
    auto *relaunch = makeButton(tr("Relancer à partir d'ici"), chainPage);
    chainLayout->addWidget(relaunch);
    connect(relaunch, &QPushButton::clicked, this, [this, stepList]() {
        const int row = qMax(0, stepList->currentRow());
        startRun(row + 1);
    });
    auto *selectAlgo = new QComboBox(chainPage);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    chainLayout->addWidget(selectAlgo);
    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
    rightTabs->addTab(chainPage, tr("Chaîne"));

    auto *infoPage = new QWidget;
    auto *infoLayout = new QVBoxLayout(infoPage);
    infoLayout->addWidget(new QLabel(tr("Projet sélectionné")));
    projectNameLabel = new QLabel;
    projectNameLabel->setWordWrap(true);
    infoLayout->addWidget(projectNameLabel);
    tileCountLabel = new QLabel;
    infoLayout->addWidget(tileCountLabel);
    auto *projectMetadata = mutedLabel(QString(), infoPage);
    projectMetadata->setObjectName(QStringLiteral("projectMetadataLabel"));
    infoLayout->addWidget(projectMetadata);
    auto *maskButton = makeButton(tr("Détecter les bords noirs"), infoPage);
    infoLayout->addWidget(maskButton);
    connect(maskButton, &QPushButton::clicked, this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Masque proposé. Vérifiez-le avant validation."));
    });
    auto *pairButton = makeButton(tr("Ouvrir la vue Paire"), infoPage);
    infoLayout->addWidget(pairButton);
    connect(pairButton, &QPushButton::clicked, this, [this]() { setMode(3); });
    auto *validateButton = makeButton(tr("Valider la paire"), infoPage, true);
    infoLayout->addWidget(validateButton);
    connect(validateButton, &QPushButton::clicked, this, [this]() {
        updateStatus(tr("Paire validée par l'opérateur; les étapes suivantes sont à refaire."));
    });
    auto *adjustButton = makeButton(tr("Saisir un décalage…"), infoPage);
    infoLayout->addWidget(adjustButton);
    connect(adjustButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Correction manuelle"));
        QFormLayout form(&dialog);
        auto *dx = new QDoubleSpinBox(&dialog);
        auto *dy = new QDoubleSpinBox(&dialog);
        dx->setRange(-100000, 100000); dx->setDecimals(1); dx->setValue(4065.5);
        dy->setRange(-100000, 100000); dy->setDecimals(1); dy->setValue(-20.7);
        form.addRow(tr("Décalage horizontal (px)"), dx);
        form.addRow(tr("Décalage vertical (px)"), dy);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            updateStatus(tr("Correction enregistrée : dx %1 px, dy %2 px.")
                         .arg(dx->value()).arg(dy->value()));
    });
    infoLayout->addStretch();
    rightTabs->addTab(infoPage, tr("Infos"));
    body->addWidget(rightTabs);
    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    body->setStretchFactor(2, 0);
    body->setSizes({42, 760, 280});

    bottomTabs = new QTabWidget;
    bottomTabs->setMinimumHeight(80);
    pairsTable = new QTableWidget(4, 8, bottomTabs);
    pairsTable->setHorizontalHeaderLabels({tr("Paire"), tr("Sens"), tr("Statut"), tr("Confiance"),
        tr("Contrôle NCC"), tr("dx (px)"), tr("dy (px)"), tr("Erreur")});
    const QStringList pairRows[] = {
        {QStringLiteral("r00_c00 → r00_c01"), tr("horizontal"), tr("douteuse"), QStringLiteral("0,91"), QStringLiteral("0,37"), QStringLiteral("4 065,5"), QStringLiteral("-20,7"), QStringLiteral("3,95 px")},
        {QStringLiteral("r00_c00 → r01_c00"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,76"), QStringLiteral("23,5"), QStringLiteral("4 086,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r00_c01 → r01_c01"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,92"), QStringLiteral("23,8"), QStringLiteral("4 133,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r01_c00 → r01_c01"), tr("horizontal"), tr("fiable"), QStringLiteral("0,98"), QStringLiteral("0,65"), QStringLiteral("4 064,7"), QStringLiteral("29,9"), QStringLiteral("0,02 px")}
    };
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col)
            pairsTable->setItem(row, col, new QTableWidgetItem(pairRows[row].at(col)));
    }
    pairsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pairsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pairsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottomTabs->addTab(pairsTable, tr("Paires (4)"));
    auto *journal = new QListWidget;
    bottomTabs->addTab(journal, tr("Journal"));
    auto *positions = new QTableWidget(4, 3);
    positions->setHorizontalHeaderLabels({tr("Tuile"), tr("X"), tr("Y")});
    for (int i = 0; i < 4; ++i) {
        positions->setItem(i, 0, new QTableWidgetItem(QStringLiteral("r%1_c%2").arg(i / 2).arg(i % 2)));
        positions->setItem(i, 1, new QTableWidgetItem(QString::number((i % 2) * 4065)));
        positions->setItem(i, 2, new QTableWidgetItem(QString::number((i / 2) * 4080)));
    }
    bottomTabs->addTab(positions, tr("Positions"));
    connect(pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { setMode(3); });
    auto *contentSplitter = new QSplitter(Qt::Vertical);
    contentSplitter->setHandleWidth(5);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->addWidget(body);
    contentSplitter->addWidget(bottomTabs);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setSizes({560, 165});
    root->addWidget(contentSplitter, 1);

    auto *status = new QWidget;
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(10, 3, 10, 3);
    zoomLabel->setMinimumWidth(48);
    statusLayout->addWidget(mutedLabel(tr("x 1 812  y 2 310 px    Tuile r00_c01"), status));
    statusLayout->addStretch();
    statusLabel = new QLabel(tr("Affichage ajusté (contraste)"));
    statusLayout->addWidget(statusLabel);
    root->addWidget(status);

    currentStepLabel = new QLabel(tr("Aucune étape"), statusBar());
    progressBar = new QProgressBar(statusBar());
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(160);
    statusBar()->addPermanentWidget(currentStepLabel);
    statusBar()->addPermanentWidget(progressBar);

    pages->addWidget(workspace);
    refreshGrid();
    refreshProjectInfo();
    connect(pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        updateStatus(tr("Paire sélectionnée : %1").arg(pairsTable->item(row, 0)->text()));
    });
}

void MainWindow::setMode(int mode)
{
    currentMode = qBound(0, mode, 5);
    pages->setCurrentWidget(workspace);
    if (rightTabs && gridTabPage) {
        const int gridIndex = rightTabs->indexOf(gridTabPage);
        if (currentMode == 1 && gridIndex < 0) {
            rightTabs->insertTab(0, gridTabPage, tr("Grille"));
            rightTabs->setCurrentWidget(gridTabPage);
        } else if (currentMode != 1 && gridIndex >= 0) {
            const bool gridWasSelected = rightTabs->currentWidget() == gridTabPage;
            rightTabs->removeTab(gridIndex);
            if (gridWasSelected && rightTabs->count() > 0)
                rightTabs->setCurrentIndex(qMin(gridIndex, rightTabs->count() - 1));
        }
    }
    if (auto *button = workspace->findChild<QPushButton *>(QStringLiteral("mode%1").arg(currentMode)))
        button->setChecked(true);
    const QStringList descriptions = {
        tr("Projet — aperçu général"), tr("Vue Grille de tuiles — organiser et vérifier l'import"),
        tr("Tuile r00_c00 — rouge : bords sombres proposés · bleu : lasso en cours"),
        tr("Vue Paire — points de contrôle et correction"), tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"),
        tr("Export — TIFF 16 bits, profil ICC conservé")
    };
    if (auto *label = workspace->findChild<QLabel *>(QStringLiteral("modeDescription")))
        label->setText(descriptions.at(currentMode));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas))
        canvasView->setMode(currentMode);
    if (projectInfoPanel)
        projectInfoPanel->setVisible(currentMode == 0);
    if (canvas)
        canvas->setVisible(currentMode != 0);
    if (auto *canvasControls = workspace->findChild<QWidget *>(
                QStringLiteral("canvasControls"))) {
        canvasControls->setVisible(currentMode != 0);
    }
    arrangementCombo->setVisible(currentMode == 1);
    if (currentMode == 1)
        bottomTabs->setCurrentIndex(1);
    if (currentMode == 3)
        bottomTabs->setCurrentIndex(0);
    updateStatus(tr("Mode : %1").arg(modeNames.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    if (projectNameLabel)
        projectNameLabel->setText(projectName);
    if (projectNameEdit)
        projectNameEdit->setText(projectName);
    if (projectFolderLabel) {
        projectFolderLabel->setText(tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(tileDirectory));
    }
    if (projectFormatLabel) {
        const QFileInfoList folderFiles = tileDirectory.isEmpty()
            ? QFileInfoList()
            : QDir(tileDirectory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                QDir::Name | QDir::IgnoreCase);
        int validFiles = 0;
        for (const QFileInfo &file : folderFiles) {
            const QString suffix = file.suffix().toLower();
            if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
                ++validFiles;
        }
        const bool allFilesValid = !folderFiles.isEmpty() && validFiles == folderFiles.size();
        projectFormatLabel->setText(allFilesValid
            ? tr("Format : tous les éléments sont au format TIFF compatible (%1 fichier(s)).")
                  .arg(validFiles)
            : tr("Format : %1 fichier(s) TIFF compatible(s) sur %2 ; certains éléments "
                "ne sont pas au bon format.")
                  .arg(validFiles).arg(folderFiles.size()));
        projectFormatLabel->setStyleSheet(allFilesValid
            ? QStringLiteral("color:#66c98a;")
            : QStringLiteral("color:#e0a45f;"));
    }
    if (tileCountLabel)
        tileCountLabel->setText(tr("%1 tuile(s) chargée(s)").arg(tilePaths.size()));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas))
        canvasView->setImagePaths(tilePaths);
    refreshGrid();
    setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}

void MainWindow::initializeGridFromPaths()
{
    if (tilePaths.size() > gridRows * gridColumns)
        gridRows = qMin(20, (static_cast<int>(tilePaths.size()) + gridColumns - 1) / gridColumns);

    const int cellCount = qMax(gridRows * gridColumns, static_cast<int>(tilePaths.size()));
    gridTiles = QVector<GridTile>(cellCount);
    const QVector<int> positions = captureOrderCells(gridRows, gridColumns, captureOrder);
    for (int index = 0; index < tilePaths.size(); ++index) {
        const int cell = index < positions.size() ? positions.at(index) : index;
        gridTiles[cell].path = tilePaths.at(index);
    }
    if (gridRowsSpin)
        gridRowsSpin->setValue(gridRows);
    if (gridColumnsSpin)
        gridColumnsSpin->setValue(gridColumns);
    selectedGridCell = 0;
}

void MainWindow::refreshGrid()
{
    while (gridTiles.size() < gridRows * gridColumns)
        gridTiles.append(GridTile{});
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas)) {
        canvasView->setGridState(gridRows, gridColumns, captureOrder,
                                 gridTiles, selectedGridCell);
    }
    refreshGridControls();
}

void MainWindow::refreshGridControls()
{
    if (gridRowsSpin) {
        const QSignalBlocker blocker(gridRowsSpin);
        gridRowsSpin->setValue(gridRows);
    }
    if (gridColumnsSpin) {
        const QSignalBlocker blocker(gridColumnsSpin);
        gridColumnsSpin->setValue(gridColumns);
    }
    if (captureOrderCombo) {
        const QSignalBlocker blocker(captureOrderCombo);
        captureOrderCombo->setCurrentIndex(captureOrder);
    }
    if (arrangementCombo) {
        const QSignalBlocker blocker(arrangementCombo);
        arrangementCombo->setCurrentIndex(captureOrder);
    }
    if (horizontalOverlapSpin) {
        const QSignalBlocker blocker(horizontalOverlapSpin);
        horizontalOverlapSpin->setValue(horizontalOverlap);
    }
    if (verticalOverlapSpin) {
        const QSignalBlocker blocker(verticalOverlapSpin);
        verticalOverlapSpin->setValue(verticalOverlap);
    }
    if (linkedOverlapCheck) {
        const QSignalBlocker blocker(linkedOverlapCheck);
        linkedOverlapCheck->setChecked(overlapsLinked);
    }
    if (modalityCombo) {
        const QSignalBlocker blocker(modalityCombo);
        const int index = modalityCombo->findText(modality);
        modalityCombo->setCurrentIndex(index >= 0 ? index : modalityCombo->findText(tr("Autre")));
        if (index < 0)
            modality = modalityCombo->currentText();
    }
    if (scaleSpin) {
        const QSignalBlocker blocker(scaleSpin);
        scaleSpin->setValue(scaleMmPerPixel);
    }
    if (automaticUncertaintyCheck) {
        const QSignalBlocker blocker(automaticUncertaintyCheck);
        automaticUncertaintyCheck->setChecked(automaticUncertainty);
    }
    if (uncertaintySpin) {
        const QSignalBlocker blocker(uncertaintySpin);
        uncertaintySpin->setValue(uncertaintyMargin);
        uncertaintySpin->setEnabled(!automaticUncertainty);
    }
    const QString scaleText = scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(scaleMmPerPixel, 0, 'g', 8) : tr("à renseigner");
    if (auto *metadata = workspace ? workspace->findChild<QLabel *>(
                QStringLiteral("projectMetadataLabel")) : nullptr) {
        metadata->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                              .arg(modality, scaleText)
                              .arg(horizontalOverlap).arg(verticalOverlap));
    }
    if (auto *title = workspace ? workspace->findChild<QLabel *>(
                QStringLiteral("workspaceTitle")) : nullptr) {
        title->setText(tr("%1 — %2 — KIK-IRPA Assemblage").arg(projectName, modality));
    }

    int activeTiles = 0;
    for (int index = 0; index < qMin(gridRows * gridColumns, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (!tile.path.isEmpty() && !tile.empty)
            ++activeTiles;
    }
    if (gridSummaryLabel) {
        const QString validationStatus = gridValidated ? tr(" · grille validée") : QString();
        gridSummaryLabel->setText(tr("%1 tuile(s) active(s) · %2 lignes × %3 colonnes%4")
                                      .arg(activeTiles).arg(gridRows).arg(gridColumns)
                                      .arg(validationStatus));
    }
    if (selectedGridTileLabel) {
        const int row = selectedGridCell / qMax(1, gridColumns);
        const int column = selectedGridCell % qMax(1, gridColumns);
        const GridTile tile = selectedGridCell >= 0 && selectedGridCell < gridTiles.size()
            ? gridTiles.at(selectedGridCell) : GridTile{};
        const QString label = QStringLiteral("r%1_c%2").arg(row, 2, 10, QLatin1Char('0'))
                                 .arg(column, 2, 10, QLatin1Char('0'));
        if (tile.path.isEmpty()) {
            selectedGridTileLabel->setText(tile.empty
                ? tr("Case sélectionnée : %1 (vide)").arg(label)
                : tr("Case sélectionnée : %1").arg(label));
        } else {
            selectedGridTileLabel->setText(
                tr("Sélection : %1 · %2 · %3° · %4")
                    .arg(label, QFileInfo(tile.path).fileName())
                    .arg(tile.rotation)
                    .arg(tile.empty ? tr("vide") : (tile.flipped ? tr("retournée") : tr("normale"))));
        }
    }
    if (emptyCellButton) {
        const bool canRestore = selectedGridCell >= 0 && selectedGridCell < gridTiles.size()
            && gridTiles.at(selectedGridCell).empty;
        emptyCellButton->setText(canRestore ? tr("Restaurer la tuile")
                                            : tr("Marquer une case vide"));
    }
    if (referencePathLabel) {
        referencePathLabel->setText(referenceImagePath.isEmpty()
            ? tr("Aucune image de référence")
            : tr("Référence : %1").arg(QFileInfo(referenceImagePath).fileName()));
        referencePathLabel->setToolTip(referenceImagePath);
    }
}

void MainWindow::swapGridTiles(int first, int second)
{
    const int cellCount = gridRows * gridColumns;
    if (first < 0 || second < 0 || first >= cellCount || second >= cellCount)
        return;
    while (gridTiles.size() <= qMax(first, second))
        gridTiles.append(GridTile{});
    std::swap(gridTiles[first], gridTiles[second]);
    selectedGridCell = second;
    gridValidated = false;
    refreshGrid();
}

void MainWindow::importReferenceImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer une image de référence"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;Tous les fichiers (*)"));
    if (path.isEmpty())
        return;
    if (readPreviewImage(path, QSize(600, 500)).isNull()) {
        QMessageBox::warning(this, tr("Image illisible"),
            tr("L'image de référence ne peut pas être ouverte."));
        return;
    }
    referenceImagePath = QFileInfo(path).absoluteFilePath();
    gridValidated = false;
    refreshGridControls();
    updateStatus(tr("Image de référence importée : %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::compareReferenceImage()
{
    if (referenceImagePath.isEmpty()) {
        importReferenceImage();
        if (referenceImagePath.isEmpty())
            return;
    }
    if (selectedGridCell < 0 || selectedGridCell >= gridTiles.size() ||
        gridTiles.at(selectedGridCell).path.isEmpty() || gridTiles.at(selectedGridCell).empty) {
        QMessageBox::information(this, tr("Aucune tuile sélectionnée"),
            tr("Sélectionnez une tuile non vide dans la grille avant de la comparer."));
        return;
    }

    const QString tilePath = gridTiles.at(selectedGridCell).path;
    const QImage reference = readPreviewImage(referenceImagePath, QSize(420, 400));
    QImage tile = readPreviewImage(tilePath, QSize(420, 400));
    if (reference.isNull() || tile.isNull()) {
        QMessageBox::warning(this, tr("Comparaison impossible"),
            tr("L'image de référence ou la tuile sélectionnée ne peut pas être lue."));
        return;
    }
    const GridTile selectedTile = gridTiles.at(selectedGridCell);
    if (selectedTile.flipped)
        tile = tile.mirrored(true, false);
    if (selectedTile.rotation != 0)
        tile = tile.transformed(QTransform().rotate(selectedTile.rotation),
                                Qt::SmoothTransformation);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comparer avec l'image de référence"));
    dialog.resize(980, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *imagesLayout = new QHBoxLayout;
    const auto addImageColumn = [&imagesLayout](const QString &title, const QString &path,
                                                const QImage &image) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        auto *name = new QLabel(title + QStringLiteral("\n") + QFileInfo(path).fileName(), column);
        name->setWordWrap(true);
        auto *preview = new QLabel(column);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(400, 440);
        preview->setStyleSheet(QStringLiteral("background:#202020;"));
        preview->setPixmap(QPixmap::fromImage(image));
        columnLayout->addWidget(name);
        columnLayout->addWidget(preview, 1);
        imagesLayout->addWidget(column, 1);
    };
    addImageColumn(tr("Image de référence"), referenceImagePath, reference);
    addImageColumn(tr("Tuile sélectionnée"), tilePath, tile);
    layout->addLayout(imagesLayout, 1);
    layout->addWidget(mutedLabel(
        tr("Comparaison visuelle côte à côte ; aucune similarité automatique n'est calculée."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    updateStatus(tr("Comparaison visuelle terminée."));
}

void MainWindow::validateGrid()
{
    QStringList errors;
    const int cellCount = gridRows * gridColumns;
    QSet<QString> assignedPaths;
    int activeTiles = 0;
    for (int index = cellCount; index < gridTiles.size(); ++index) {
        if (!gridTiles.at(index).path.isEmpty())
            errors.append(tr("La tuile %1 est hors de la grille.")
                              .arg(QFileInfo(gridTiles.at(index).path).fileName()));
    }
    for (int index = 0; index < qMin(cellCount, static_cast<int>(gridTiles.size())); ++index) {
        const GridTile &tile = gridTiles.at(index);
        if (tile.path.isEmpty())
            continue;
        const QString absolutePath = QFileInfo(tile.path).absoluteFilePath();
        if (assignedPaths.contains(absolutePath)) {
            errors.append(tr("La même image est affectée à plusieurs cases : %1")
                              .arg(QFileInfo(tile.path).fileName()));
        }
        assignedPaths.insert(absolutePath);
        if (!QFileInfo::exists(tile.path))
            errors.append(tr("Fichier introuvable : %1").arg(tile.path));
        else if (readPreviewImage(tile.path, QSize(32, 32)).isNull())
            errors.append(tr("Image illisible : %1").arg(tile.path));
        if (!tile.empty)
            ++activeTiles;
    }
    for (const QString &path : tilePaths) {
        if (!assignedPaths.contains(QFileInfo(path).absoluteFilePath()))
            errors.append(tr("Image non placée dans la grille : %1")
                              .arg(QFileInfo(path).fileName()));
    }
    if (activeTiles == 0)
        errors.append(tr("La grille doit contenir au moins une tuile active."));

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("Grille à corriger"), errors.join(QLatin1Char('\n')));
        updateStatus(tr("La grille n'est pas valide : %1 problème(s).").arg(errors.size()));
        gridValidated = false;
        refreshGridControls();
        return;
    }

    gridValidated = true;
    refreshGridControls();
    updateStatus(tr("Grille validée : %1 tuile(s), %2 × %3, ordre « %4 ».")
                     .arg(activeTiles).arg(gridRows).arg(gridColumns)
                     .arg(captureOrderCombo ? captureOrderCombo->currentText() : QString()));
    QMessageBox::information(this, tr("Grille validée"),
        tr("La grille et ses paramètres sont valides. Enregistrez le projet pour conserver les réglages."));
}

void MainWindow::importTiles()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, tr("Sélectionner un dossier de photos"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directoryPath.isEmpty())
        return;

    const QDir directory(directoryPath);
    const QStringList imageFilters = {QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
    const QFileInfoList imageFiles = directory.entryInfoList(
        imageFilters, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    if (imageFiles.isEmpty()) {
        QMessageBox::warning(this, tr("Aucune photo trouvée"),
            tr("Le dossier sélectionné ne contient aucune image TIFF compatible "
               "(.tif ou .tiff)."));
        return;
    }

    QStringList paths;
    paths.reserve(imageFiles.size());
    for (const QFileInfo &imageFile : imageFiles)
        if (!tilePaths.contains(imageFile.absoluteFilePath()))
            paths.append(imageFile.absoluteFilePath());

    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("Photos déjà importées"),
            tr("Toutes les photos de ce dossier sont déjà dans le projet."));
        return;
    }

    tilePaths.append(paths);
    tileDirectory = directoryPath;
    const int requiredRows = (static_cast<int>(tilePaths.size()) + gridColumns - 1) / gridColumns;
    gridRows = qMax(gridRows, qMin(20, requiredRows));
    while (gridTiles.size() < gridRows * gridColumns)
        gridTiles.append(GridTile{});
    const QVector<int> positions = captureOrderCells(gridRows, gridColumns, captureOrder);
    int positionIndex = 0;
    for (const QString &path : paths) {
        while (positionIndex < positions.size() &&
               !gridTiles.at(positions.at(positionIndex)).path.isEmpty())
            ++positionIndex;
        if (positionIndex >= positions.size()) {
            GridTile overflowTile;
            overflowTile.path = path;
            gridTiles.append(overflowTile);
            continue;
        }
        gridTiles[positions.at(positionIndex)].path = path;
        ++positionIndex;
    }
    gridValidated = false;
    refreshProjectInfo();
    setMode(1);
    updateStatus(tr("%1 photo(s) importée(s) depuis « %2 ». Les images source ne sont pas modifiées.")
                 .arg(paths.size()).arg(directory.dirName()));
}

void MainWindow::saveProject()
{
    QString path = projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"),
            projectName + QStringLiteral(".kikproj"), tr("Projet KIK-IRPA (*.kikproj);;JSON (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject project;
    project.insert(QStringLiteral("format_version"), QStringLiteral("1.0"));
    project.insert(QStringLiteral("project_name"), projectName);
    project.insert(QStringLiteral("saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), currentMode);
    project.insert(QStringLiteral("tile_directory"), tileDirectory);
    project.insert(QStringLiteral("output_directory"), outputDirectory);
    project.insert(QStringLiteral("grid_rows"), gridRows);
    project.insert(QStringLiteral("grid_columns"), gridColumns);
    project.insert(QStringLiteral("capture_order"), captureOrder);
    project.insert(QStringLiteral("horizontal_overlap"), horizontalOverlap);
    project.insert(QStringLiteral("vertical_overlap"), verticalOverlap);
    project.insert(QStringLiteral("overlaps_linked"), overlapsLinked);
    project.insert(QStringLiteral("modality"), modality);
    project.insert(QStringLiteral("scale_mm_per_pixel"), scaleMmPerPixel);
    project.insert(QStringLiteral("uncertainty_margin"), uncertaintyMargin);
    project.insert(QStringLiteral("automatic_uncertainty"), automaticUncertainty);
    project.insert(QStringLiteral("reference_image"), referenceImagePath);
    QJsonArray tiles;
    for (const QString &tile : tilePaths)
        tiles.append(tile);
    project.insert(QStringLiteral("tiles"), tiles);
    QJsonArray grid;
    for (const GridTile &tile : gridTiles) {
        QJsonObject cell;
        cell.insert(QStringLiteral("path"), tile.path);
        cell.insert(QStringLiteral("rotation"), tile.rotation);
        cell.insert(QStringLiteral("flipped"), tile.flipped);
        cell.insert(QStringLiteral("empty"), tile.empty);
        grid.append(cell);
    }
    project.insert(QStringLiteral("grid_tiles"), grid);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
            tr("Le projet n'a pas pu être enregistré à cet emplacement."));
        return;
    }
    projectPath = path;
    projectCreated = true;
    rememberRecentProject(path);
    updateStatus(tr("Projet enregistré : %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"),
        QString(), tr("Projet KIK-IRPA (*.kikproj *.json);;Tous les fichiers (*)"));
    if (path.isEmpty())
        return;
    loadProjectFromPath(path);
}

bool MainWindow::loadProjectFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ouverture impossible"), tr("Le fichier ne peut pas être lu."));
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Projet invalide"),
            tr("Ce fichier n'est pas un projet JSON valide."));
        return false;
    }
    const QJsonObject project = document.object();
    projectName = project.value(QStringLiteral("project_name")).toString(QFileInfo(path).completeBaseName());
    tileDirectory = project.value(QStringLiteral("tile_directory")).toString();
    outputDirectory = project.value(QStringLiteral("output_directory")).toString();
    gridRows = qBound(1, project.value(QStringLiteral("grid_rows")).toInt(3), 20);
    gridColumns = qBound(1, project.value(QStringLiteral("grid_columns")).toInt(2), 20);
    captureOrder = qBound(0, project.value(QStringLiteral("capture_order")).toInt(0), 5);
    horizontalOverlap = qBound(0, project.value(QStringLiteral("horizontal_overlap")).toInt(20), 90);
    verticalOverlap = qBound(0, project.value(QStringLiteral("vertical_overlap")).toInt(20), 90);
    overlapsLinked = project.value(QStringLiteral("overlaps_linked")).toBool(true);
    if (overlapsLinked)
        verticalOverlap = horizontalOverlap;
    modality = project.value(QStringLiteral("modality")).toString(QStringLiteral("Infrarouge"));
    scaleMmPerPixel = qMax(0.0, project.value(QStringLiteral("scale_mm_per_pixel")).toDouble());
    uncertaintyMargin = qMax(0.0, project.value(QStringLiteral("uncertainty_margin")).toDouble());
    automaticUncertainty = project.value(QStringLiteral("automatic_uncertainty")).toBool(true);
    referenceImagePath = project.value(QStringLiteral("reference_image")).toString();
    tilePaths.clear();
    for (const QJsonValue &tile : project.value(QStringLiteral("tiles")).toArray())
        tilePaths.append(tile.toString());
    const QJsonArray grid = project.value(QStringLiteral("grid_tiles")).toArray();
    if (grid.isEmpty()) {
        initializeGridFromPaths();
    } else {
        gridTiles.clear();
        gridTiles.reserve(qMax(gridRows * gridColumns, static_cast<int>(grid.size())));
        for (const QJsonValue &cellValue : grid) {
            const QJsonObject cell = cellValue.toObject();
            GridTile tile;
            tile.path = cell.value(QStringLiteral("path")).toString();
            const int savedRotation = cell.value(QStringLiteral("rotation")).toInt();
            tile.rotation = ((savedRotation % 360) + 360) % 360;
            tile.flipped = cell.value(QStringLiteral("flipped")).toBool();
            tile.empty = cell.value(QStringLiteral("empty")).toBool();
            gridTiles.append(tile);
        }
        while (gridTiles.size() < gridRows * gridColumns)
            gridTiles.append(GridTile{});
    }
    selectedGridCell = 0;
    gridValidated = false;
    projectPath = path;
    projectCreated = true;
    refreshProjectInfo();
    pages->setCurrentWidget(workspace);
    setMode(0);
    rememberRecentProject(path);
    updateStatus(tr("Projet ouvert sans recalcul. Vérifiez les chemins des tuiles."));
    return true;
}

void MainWindow::startRun(int firstStep)
{
    if (progressRunning) {
        updateStatus(tr("Une démonstration de progression est déjà en cours."));
        return;
    }
    if (tilePaths.isEmpty()) {
        const auto answer = QMessageBox::question(this, tr("Aucune tuile importée"),
            tr("Aucune tuile n'est chargée. Lancer quand même la démonstration d'interface ?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }
    if (firstStep > 1) {
        QDialog confirmation(this);
        confirmation.setWindowTitle(tr("Relancer à partir de l'étape %1").arg(firstStep));
        auto *layout = new QVBoxLayout(&confirmation);
        layout->addWidget(new QLabel(tr("Les étapes précédentes seront réutilisées sans recalcul."), &confirmation));
        auto *reused = new QListWidget(&confirmation);
        for (int step = 1; step < firstStep; ++step)
            reused->addItem(tr("✓  Étape %1 — résultat conservé").arg(step));
        layout->addWidget(reused);
        auto *recalculated = new QListWidget(&confirmation);
        for (int step = firstStep; step <= 8; ++step)
            recalculated->addItem(tr("↻  Étape %1 — résultat recalculé").arg(step));
        layout->addWidget(recalculated);
        layout->addWidget(mutedLabel(
            tr("Les résultats actuels seront conservés dans l'historique. "
               "Cette maquette ne lance pas de calcul backend."), &confirmation));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                             &confirmation);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Lancer"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
        if (confirmation.exec() != QDialog::Accepted)
            return;
    }
    progressRunning = true;
    progressBar->setValue(0);
    currentStepLabel->setText(tr("Démonstration"));
    updateStatus(tr("Démonstration de progression — le backend de calcul n'est pas connecté."));
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer, firstStep]() {
        progressBar->setValue(progressBar->value() + 4);
        if (progressBar->value() >= 100) {
            timer->stop();
            timer->deleteLater();
            progressRunning = false;
            currentStepLabel->setText(tr("Terminé"));
            updateStatus(tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
                         .arg(firstStep));
        }
    });
    timer->start(100);
    QTimer::singleShot(150, this, [this, timer]() {
        if (timer->isActive()) {
            auto *cancel = makeButton(tr("Annuler"), this);
            cancel->setObjectName(QStringLiteral("cancelRunButton"));
            statusBar()->addPermanentWidget(cancel);
            connect(cancel, &QPushButton::clicked, this, [this, timer, cancel]() {
                timer->stop();
                timer->deleteLater();
                progressRunning = false;
                currentStepLabel->setText(tr("Annulée"));
                cancel->deleteLater();
                updateStatus(tr("Démonstration annulée."));
            });
            connect(timer, &QTimer::destroyed, cancel, &QPushButton::deleteLater);
        }
    });
}

void MainWindow::showComparison()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comparer deux exécutions"));
    dialog.resize(440, 240);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Comparer les sorties de deux algorithmes de couture."), &dialog));
    auto *algorithms = new QHBoxLayout;
    auto *a = new QComboBox(&dialog);
    auto *b = new QComboBox(&dialog);
    const QStringList choices = {tr("Programmation dynamique"), tr("Coupe de graphe"), tr("Dijkstra")};
    a->addItems(choices);
    b->addItems(choices);
    b->setCurrentIndex(1);
    algorithms->addWidget(a);
    algorithms->addWidget(b);
    layout->addLayout(algorithms);
    layout->addWidget(new QLabel(tr("Mesures : saut à la couture · détails conservés · durée"), &dialog));
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *show = buttons->addButton(tr("Comparer"), QDialogButtonBox::ActionRole);
    auto *keepA = buttons->addButton(tr("Garder A"), QDialogButtonBox::ActionRole);
    auto *keepB = buttons->addButton(tr("Garder B"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(show, &QPushButton::clicked, this, [this, &dialog]() {
        comparisonVisible = !comparisonVisible;
        static_cast<ArtworkCanvas *>(canvas)->toggleComparison();
        updateStatus(comparisonVisible ? tr("Comparaison affichée — curseur de séparation actif.")
                                       : tr("Comparaison côte à côte affichée."));
        dialog.accept();
    });
    connect(keepA, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat A conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    connect(keepB, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat B conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::showExportDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exporter le projet"));
    QVBoxLayout layout(&dialog);
    auto *format = new QComboBox(&dialog);
    format->addItems({tr("TIFF 16 bits"), tr("TIFF 8 bits — perte de précision"),
                      tr("PSB"), tr("JPEG — perte de précision")});
    auto *report = new QCheckBox(tr("Générer le rapport d'assemblage (HTML)"), &dialog);
    report->setChecked(true);
    layout.addWidget(new QLabel(tr("Format de sortie"), &dialog));
    layout.addWidget(format);
    layout.addWidget(new QLabel(tr("Profil ICC des tuiles conservé"), &dialog));
    layout.addWidget(report);
    layout.addWidget(mutedLabel(tr("L'export de la mosaïque nécessite le backend, non connecté dans ce prototype."), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportReport = buttons->addButton(tr("Enregistrer le rapport…"), QDialogButtonBox::AcceptRole);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportReport, &QPushButton::clicked, this, [this, &dialog, format, report]() {
        if (!report->isChecked()) {
            QMessageBox::information(&dialog, tr("Backend requis"),
                tr("La génération de l'image mosaïque sera disponible une fois le backend connecté."));
            return;
        }
        const QString reportName = projectName + QStringLiteral("_rapport.html");
        const QString defaultPath = outputDirectory.isEmpty()
            ? reportName : QDir(outputDirectory).filePath(reportName);
        const QString path = QFileDialog::getSaveFileName(&dialog, tr("Enregistrer le rapport"),
            defaultPath, tr("Rapport HTML (*.html)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const QString html = QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Rapport KIK-IRPA</title>"
            "<h1>Rapport d'assemblage — %1</h1><p>Généré le %2.</p><p>Format demandé : %3</p>"
            "<p>Tuiles référencées : %4</p><p>Les mesures présentées sont celles de la maquette.</p>")
            .arg(projectName.toHtmlEscaped(),
                 QDateTime::currentDateTime().toString(Qt::ISODate).toHtmlEscaped(),
                 format->currentText().toHtmlEscaped()).arg(tilePaths.size());
        if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0 || !file.commit()) {
            QMessageBox::warning(&dialog, tr("Export impossible"), tr("Le rapport n'a pas pu être enregistré."));
            return;
        }
        updateStatus(tr("Rapport HTML exporté."));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::updateStatus(const QString &message)
{
    if (statusLabel)
        statusLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::updateProgress(const QString &stepName, int percentage)
{
    currentStepLabel->setText(stepName);
    progressBar->setValue(qBound(0, percentage, 100));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab) {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
