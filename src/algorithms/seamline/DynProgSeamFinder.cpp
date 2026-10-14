#include "algorithms/seamline/DynProgSeamFinder.h"

namespace stitching::seamline {

DynProgSeamFinder::DynProgSeamFinder(const Json& params) : params(params) {}

bool DynProgSeamFinder::find(const cv::Mat&, const cv::Mat&, const PairContext&, cv::Mat&) {
    // TODO : seamline par programmation dynamique
    return true;
}

} // namespace stitching::seamline
