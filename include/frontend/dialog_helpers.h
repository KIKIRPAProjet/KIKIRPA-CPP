// include/frontend/dialog_helpers.h
#pragma once

#include <QString>

class QImage;
class PipelineRunnerClass;
class QWidget;

namespace DialogHelpers
{
// Affiche les deux images et leurs chemins dans une fenêtre de comparaison.
void showReferenceComparisonDialog(QWidget *parent, const QString &referencePath,
                                   const QString &tilePath, const QImage &referenceImage,
                                   const QImage &tileImage);
void showComparisonDialog(QWidget *parent, PipelineRunnerClass *pipelineRunner);
bool confirmRunRestart(QWidget *parent, int firstStep);
}
