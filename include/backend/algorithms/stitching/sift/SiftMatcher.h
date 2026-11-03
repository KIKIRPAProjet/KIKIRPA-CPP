#pragma once
#include "algorithms/legacy_type_aliases.h"
#include "algorithms/stitching/feature_matcher.h"

namespace stitching::matching {

class SiftMatcher : public StitchingNs::FeatureMatcherClass {
    Json params;
public:
    explicit SiftMatcher(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};

} // namespace stitching::matching
