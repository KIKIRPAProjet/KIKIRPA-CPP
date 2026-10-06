#include "algorithms/seamline/GraphCutSeamFinder.h"

namespace stitching::seamline {

GraphCutSeamFinder::GraphCutSeamFinder(const Json& params) : params(params) {}

bool GraphCutSeamFinder::find(const cv::Mat&, const cv::Mat&, const PairContext&, cv::Mat&) {
    // TODO : seamline par graph cut
    return true;
}

} // namespace stitching::seamline
