#pragma once
#include "core/data/algorithm_types.h"
#include "core/step/step.h"
namespace StepsNs {
class AssemblyStepClass final : public StepNs::StepClass { public: AssemblyStepClass(); bool execute() override; };
}
