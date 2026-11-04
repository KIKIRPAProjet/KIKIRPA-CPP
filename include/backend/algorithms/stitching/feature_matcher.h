// include/backend/algorithms/stitching/feature_matcher.h
#pragma once

#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

namespace StitchingNs {

    // À implémenter par chaque méthode d'appariement (corrélation de phase, SIFT, LightGlue...).
    class FeatureMatcherClass {

        public:
            virtual ~FeatureMatcherClass() = default;

            // Apparie la tuile B à la tuile A, voisines dans la direction context.direction. false si la paire ne
            // peut pas être mesurée.
            virtual bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                               DataNs::Matches& matches) = 0;

    };

}
