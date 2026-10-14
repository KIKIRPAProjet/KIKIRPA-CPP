#include "algorithms/seamline/GraphCutPairSeamFinder.h"

namespace stitching::seamline {

GraphCutPairSeamFinder::GraphCutPairSeamFinder(const Json& params) : params(params) {}

bool GraphCutPairSeamFinder::find(const cv::Mat&, const cv::Mat&, const PairContext&, cv::Mat&) {
    // TODO : seamline par graph cut
    return true;
}

} // namespace stitching::seamline
