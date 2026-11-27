#pragma once

#include "frontendutils.h"

#include <QString>
#include <vector>

class QImage;
class PipelineRunnerClass;
class QWidget;

namespace DialogHelpers
{
void showReferenceComparisonDialog(QWidget *parent, const QString &referencePath,
                                   const QString &tilePath, const QImage &referenceImage,
                                   const QImage &tileImage);
void showComparisonDialog(QWidget *parent, PipelineRunnerClass *pipelineRunner);
bool confirmRunRestart(QWidget *parent, int firstStep,
                       const std::vector<FrontendUtils::PipelineStepInfo> &steps);
void showCreditsDialog(QWidget *parent);
}
