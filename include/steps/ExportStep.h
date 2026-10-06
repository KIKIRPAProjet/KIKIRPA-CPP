#pragma once
#include "core/Step.h"

namespace stitching::steps {

class ExportStep : public Step {
public: bool execute() override;
};

} // namespace stitching::steps
