#include "algorithms/matching/HarrisNccMatcher.h"

namespace stitching::matching {

HarrisNccMatcher::HarrisNccMatcher(const Json& params) : params(params) {}

bool HarrisNccMatcher::match(const cv::Mat&, const cv::Mat&, const PairContext&, Matches&) {
    // TODO : coins de Harris + corrélation NCC
    return true;
}

} // namespace stitching::matching
