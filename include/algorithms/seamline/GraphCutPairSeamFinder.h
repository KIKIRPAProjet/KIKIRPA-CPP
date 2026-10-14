#pragma once
#include "algorithms/seamline/SeamFinder.h"

namespace stitching::seamline {

class GraphCutPairSeamFinder : public SeamFinder {
    Json params;
public:
    explicit GraphCutPairSeamFinder(const Json& params = {});
    bool find(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, cv::Mat& seamMask) override;
};

} // namespace stitching::seamline
