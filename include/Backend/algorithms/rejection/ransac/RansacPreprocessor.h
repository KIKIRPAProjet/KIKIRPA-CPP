#pragma once

#include "algorithms/rejection/RansacTypes.h"

namespace stitching::rejection::ransac {

class RansacPreprocessor {
public:
    bool prepare(
        const RansacInput& input,
        const RansacParameters& params,
        RansacPreparedInput& output
    ) const;

private:
    static bool isFinite(
        const cv::Point2f& point
    );
};

} // namespace stitching::rejection::ransac