#pragma once
#include "algorithms/matching/FeatureMatcher.h"

namespace stitching::matching {

class SiftMatcher : public FeatureMatcher {
    Json params;
public:
    explicit SiftMatcher(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};

} // namespace stitching::matching
