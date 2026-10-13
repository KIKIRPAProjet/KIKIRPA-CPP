#include "algorithms/matching/SiftMatcher.h"
#include "algorithms/matching/SiftMatcherAlgorithm.h"

namespace stitching::matching {

SiftMatcher::SiftMatcher(const Json& params)
    : params(params.is_null() ? Json::object() : params) {}

bool SiftMatcher::match(const cv::Mat& a, const cv::Mat& b,
                        const PairContext& ctx, Matches& out) {
    return runSiftMatching(a, b, ctx, params, out);
}

} // namespace stitching::matching
