#pragma once
#include "core/Types.h"

namespace stitching::matching {

class FeatureMatcher {
public:
    virtual ~FeatureMatcher() = default;
    virtual bool match(const cv::Mat& a, const cv::Mat& b,
                       const PairContext& ctx, Matches& out) = 0;
};

} // namespace stitching::matching
