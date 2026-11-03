#pragma once
#include "core/data/algorithm_types.h"
#include "core/step/step.h"
namespace StepsNs { class ExportStepClass final : public StepNs::StepClass { public: ExportStepClass(); bool execute() override; }; }
