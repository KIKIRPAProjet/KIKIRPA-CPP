#pragma once

#include "core/Types.h"

namespace stitching::matching {

bool runSiftMatching(const cv::Mat& a, const cv::Mat& b,
                     const PairContext& ctx, const Json& params, Matches& out);

} // namespace stitching::matching
