// include/backend/algorithms/stitching/FeatureMatcher.h
#pragma once
#include "algorithms/legacy_type_aliases.h"

namespace stitching::matching {

// À implémenter par chaque méthode d'appariement (SIFT, Harris-NCC, LightGlue...)
class FeatureMatcher {
public:
    virtual ~FeatureMatcher() = default;
    virtual bool match(const cv::Mat& a, const cv::Mat& b,
                       const PairContext& ctx, Matches& out) = 0;
};

} // namespace stitching::matching
