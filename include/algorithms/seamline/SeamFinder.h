#pragma once
#include "core/Types.h"

namespace stitching::seamline {

// À implémenter par chaque méthode de seamline (programmation dynamique, graph cut...)
// seamMask : 255 là où on garde a, 0 là où on garde b
class SeamFinder {
public:
    virtual ~SeamFinder() = default;
    virtual bool find(const cv::Mat& a, const cv::Mat& b,
                      const PairContext& ctx, cv::Mat& seamMask) = 0;
};

} // namespace stitching::seamline
