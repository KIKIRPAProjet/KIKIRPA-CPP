// src/backend/algorithms/stitching/sift/sift_matcher.cpp
#include "algorithms/stitching/sift/sift_matcher.h"

#include "algorithms/stitching/sift/sift_matcher_algorithm.h"

namespace SiftNs {

    SiftMatcherClass::SiftMatcherClass(const Json& initialParameters)
        : parameters(initialParameters.is_null() ? Json::object() : initialParameters) {}

    bool SiftMatcherClass::match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                                 DataNs::Matches& matches) {
        return runSiftMatching(imageA, imageB, context, parameters, matches);
    }

}
