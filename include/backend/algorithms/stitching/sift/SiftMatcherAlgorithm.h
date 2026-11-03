// include/backend/algorithms/stitching/sift/SiftMatcherAlgorithm.h
#pragma once

#include "algorithms/legacy_type_aliases.h"

namespace stitching::matching {

bool runSiftMatching(const cv::Mat& a, const cv::Mat& b,
                     const PairContext& ctx, const Json& params, Matches& out);

} // namespace stitching::matching
