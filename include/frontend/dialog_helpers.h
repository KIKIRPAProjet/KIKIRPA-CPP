#ifndef DIALOG_HELPERS_H
#define DIALOG_HELPERS_H

#include <QString>

class QImage;
class PipelineRunnerClass;
class QWidget;

namespace DialogHelpers
{
void showReferenceComparisonDialog(QWidget *parent, const QString &referencePath,
                                   const QString &tilePath, const QImage &referenceImage,
                                   const QImage &tileImage);
void showComparisonDialog(QWidget *parent, PipelineRunnerClass *pipelineRunner);
bool confirmRunRestart(QWidget *parent, int firstStep);
}

#endif
