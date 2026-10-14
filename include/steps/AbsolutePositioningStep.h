#pragma once
#include "core/Step.h"

namespace stitching::steps {

class AbsolutePositioningStep : public Step {
public: bool execute() override;  // positions relatives → absolues + taille canvas
};

} // namespace stitching::steps
