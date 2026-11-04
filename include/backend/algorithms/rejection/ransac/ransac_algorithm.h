// include/backend/algorithms/rejection/ransac/ransac_algorithm.h
#pragma once

#include "algorithms/rejection/ransac/ransac_types.h"

namespace RansacNs {

    class RansacAlgorithmClass {

        public:
            // Similitude partielle B -> A (cv::estimateAffinePartial2D, graine fixe : résultat reproductible), en
            // matrice homogène 3x3 CV_64F. Inliers rangés aux indices des DataNs::Matches d'origine. false si les
            // données préparées sont incohérentes, si l'estimation échoue ou s'il y a moins de
            // parameters.minimumInlierCount inliers.
            bool estimate(const RansacPreparedInput& input, const RansacParameters& parameters, RansacOutput& output) const;

    };

}
