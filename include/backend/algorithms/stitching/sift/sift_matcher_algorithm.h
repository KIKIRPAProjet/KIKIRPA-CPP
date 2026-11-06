// include/backend/algorithms/stitching/sift/sift_matcher_algorithm.h
#pragma once

#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

namespace SiftNs {

    // Calcul de SiftMatcherClass::match (voir sift_matcher.h pour les réglages et la sortie). Les images sont les
    // bandes de recouvrement déjà découpées, 8 ou 16 bits, 1, 3 ou 4 canaux ; context est ignoré.
    bool runSiftMatching(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                         const nlohmann::json& parameters, DataNs::Matches& matches);

}
