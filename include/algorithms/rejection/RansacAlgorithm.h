#pragma once

#include "algorithms/rejection/RansacTypes.h"

namespace stitching::rejection::ransac {

class RansacAlgorithm {
public:
    bool estimate(
        const RansacPreparedInput& input,
        const RansacParameters& params,
        RansacOutput& output
    ) const;
};

} // namespace stitching::rejection::ransac