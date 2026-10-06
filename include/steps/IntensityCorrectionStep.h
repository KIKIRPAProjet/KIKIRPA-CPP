#pragma once
#include "core/Step.h"

namespace stitching::steps {

class IntensityCorrectionStep : public Step {
public: bool execute() override;
};

} // namespace stitching::steps
