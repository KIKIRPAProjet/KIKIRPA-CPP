/*
 * .cpp : le POURQUOI
 * - choix de conception : regrouper les composants graphiques communs évite de dupliquer leur style ;
 * - cas limite : le bouton peut être primaire ou secondaire selon le contexte de l'interface ;
 * - unité ou repère : les dimensions et propriétés suivent les règles visuelles du frontend ;
 * - raison non évidente : les propriétés Qt permettent au style global de personnaliser les widgets.
 */
/*
 * Header : le QUOI
 * Fournit les outils communs de l'interface frontend.
 * - ce que ça fait : crée les boutons, les libellés et les éléments graphiques réutilisables ;
 * - ce que ça renvoie : les widgets Qt configurés pour être utilisés par l'interface ;
 * - quand ça lève une exception : si la création ou la configuration d'un widget échoue.
 */
// fournit les outils communs de linterface frontend
#include "frontendutils.h"

#include <QImageReader>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QtMath>

#include <cmath>

#ifdef Q_OS_WIN
#include <wincodec.h>
#include <windows.h>
#endif

namespace FrontendUtils {

const QStringList MODE_NAMES = {
    QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
    QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Export")
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary)
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

QString formatZoom(double zoomPercent)
{
    if (std::abs(zoomPercent - std::round(zoomPercent)) < 0.05) {
        return QString::number(static_cast<int>(std::round(zoomPercent))) + QStringLiteral(" %");
    }
    QString text = QString::number(zoomPercent, 'f', zoomPercent < 10.0 ? 2 : 1);
    while (text.contains(QLatin1Char('.')) &&
           (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
            break;
        }
        text.chop(1);
    }
    return text + QStringLiteral(" %");
}

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
