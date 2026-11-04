// src/backend/algorithms/rejection/ransac/ransac_algorithm.cpp
#include "algorithms/rejection/ransac/ransac_algorithm.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <cstddef>

namespace RansacNs {

    bool RansacAlgorithmClass::estimate(const RansacPreparedInput& input, const RansacParameters& parameters,
                                        RansacOutput& output) const {
        // 1. Nettoyage de la sortie
        output.transformBToA.release();
        output.inlierMask.assign(input.originalCount, 0);
        output.inlierCount = 0;

        // 2. Vérification des données préparées
        if (input.sourcePoints.size() != input.destinationPoints.size()) return false;
        if (input.sourcePoints.size() != input.originalIndices.size()) return false;
        if (input.sourcePoints.size() < static_cast<std::size_t>(parameters.minimumMatchCount)) return false;

        // 3. Algorithme RANSAC : source = B, destination = A, transformation cherchée B -> A.
        // Réinitialisation de la graine du générateur pseudo-aléatoire OpenCV : garantit la reproductibilité stricte
        // (déterminisme § 4.4 du cahier des charges) pour RANSAC.
        cv::Mat inlierMatrix;
        cv::theRNG().state = 0xffffffff;
        const cv::Mat affine = cv::estimateAffinePartial2D(input.sourcePoints, input.destinationPoints, inlierMatrix,
                                                           cv::RANSAC, parameters.reprojectionThreshold,
                                                           parameters.maximumIterationCount, parameters.confidence,
                                                           parameters.refinementIterationCount);

        // 4. Vérification du résultat
        if (affine.empty() || inlierMatrix.empty()) return false;
        if (inlierMatrix.total() != input.sourcePoints.size()) return false;

        // 5. Récupération des inliers : on remet les résultats aux indices correspondant aux Matches d'origine.
        const cv::Mat flatInlierMatrix = inlierMatrix.reshape(1, 1);
        const uchar* inlierValues = flatInlierMatrix.ptr<uchar>(0);
        for (std::size_t pointIndex = 0; pointIndex < input.sourcePoints.size(); ++pointIndex) {
            if (inlierValues[pointIndex] == 0) continue;
            output.inlierMask[input.originalIndices[pointIndex]] = 1;
            ++output.inlierCount;
        }

        // 6. Nombre minimum d'inliers
        if (output.inlierCount < parameters.minimumInlierCount) {
            output.inlierMask.assign(input.originalCount, 0);
            output.inlierCount = 0;
            return false;
        }

        // 7. Conversion de la matrice OpenCV : estimateAffinePartial2D renvoie une matrice 2x3
        //   [ a b tx ]
        //   [ c d ty ]
        // et DataNs::Alignment attend une matrice homogène 3x3 en CV_64F :
        //   [ a b tx ]
        //   [ c d ty ]
        //   [ 0 0  1 ]
        cv::Mat affine64;
        affine.convertTo(affine64, CV_64F);
        output.transformBToA = cv::Mat::eye(3, 3, CV_64F);
        affine64.copyTo(output.transformBToA(cv::Rect(0, 0, 3, 2)));
        return true;
    }

}
