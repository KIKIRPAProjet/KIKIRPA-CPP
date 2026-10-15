#include "algorithms/stitching/sift/SiftMatcher.h"
#include "algorithms/stitching/sift/SiftMatcherAlgorithm.h"

namespace stitching::matching {

SiftMatcher::SiftMatcher(const Json& params)
    : params(params.is_null() ? Json::object() : params) {}

bool SiftMatcher::match(const cv::Mat& a, const cv::Mat& b,
                        const PairContext& ctx, Matches& out) {
    return runSiftMatching(a, b, ctx, params, out);
}

} // namespace stitching::matching
