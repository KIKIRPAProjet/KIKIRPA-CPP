#ifndef FRONTENDUTILS_H
#define FRONTENDUTILS_H

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QPushButton;
class QWidget;

namespace FrontendUtils {

extern const QStringList modeNames;

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false);
QLabel *mutedLabel(const QString &text, QWidget *parent);

}

QVector<int> captureOrderCells(int rows, int columns, int order);
QImage readPreviewImage(const QString &path, const QSize &maximumSize);

#endif
