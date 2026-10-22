
#pragma once

#include "core/Step.h"

namespace stitching::steps {

class RansacStep : public Step {
public:
    explicit RansacStep(const Json& params = Json::object());

    bool execute() override;
};

} // namespace stitching::steps
