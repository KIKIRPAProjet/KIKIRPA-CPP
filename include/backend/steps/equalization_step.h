#pragma once
#include "core/data/algorithm_types.h"
#include "core/step/step.h"
namespace StepsNs {
class EqualizationStepClass final : public StepNs::StepClass {
public: EqualizationStepClass(); bool execute() override;
};
}
