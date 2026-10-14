#pragma once
#include "core/Step.h"

namespace stitching::steps {

class FusionStep : public Step {
public: bool execute() override;
};

} // namespace stitching::steps
