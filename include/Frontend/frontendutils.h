#ifndef FRONTENDUTILS_H
#define FRONTENDUTILS_H

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

QVector<int> captureOrderCells(int rows, int columns, int order);
QImage readPreviewImage(const QString &path, const QSize &maximumSize);

#endif
