#pragma once
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

namespace StepsNs {
class PositionStepClass final : public StepNs::StepClass {
public:
    PositionStepClass();
    bool execute() override;
};
}
