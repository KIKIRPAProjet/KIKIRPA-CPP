#include "algorithms/matching/LightGlueMatcher.h"

namespace stitching::matching {

LightGlueMatcher::LightGlueMatcher(const Json& params) : params(params) {}

bool LightGlueMatcher::match(const cv::Mat&, const cv::Mat&, const PairContext&, Matches&) {
    // TODO : appariement LightGlue
    return true;
}

} // namespace stitching::matching
