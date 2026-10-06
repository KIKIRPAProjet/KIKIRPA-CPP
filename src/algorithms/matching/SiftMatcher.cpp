#include "algorithms/matching/SiftMatcher.h"

namespace stitching::matching {

SiftMatcher::SiftMatcher(const Json& params) : params(params) {}

bool SiftMatcher::match(const cv::Mat&, const cv::Mat&, const PairContext&, Matches&) {
    // TODO : détection + appariement SIFT
    return true;
}

} // namespace stitching::matching
