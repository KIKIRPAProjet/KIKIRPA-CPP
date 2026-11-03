#include "algorithms/rejection/ransac/RansacAlgorithm.h"

#include <opencv2/calib3d.hpp>

namespace stitching::rejection::ransac {

bool RansacAlgorithm::estimate(
    const RansacPreparedInput& input,
    const RansacParameters& params,
    RansacOutput& output
) const
{
    // --------------------------------------------------------
    // 1. Nettoyage de la sortie
    // --------------------------------------------------------

    output.transform.release();

    output.inliers.assign(
        input.originalCount,
        0
    );

    output.inlierCount = 0;

    // --------------------------------------------------------
    // 2. Vérification des données préparées
    // --------------------------------------------------------

    if (
        input.sourcePoints.size()
        != input.destinationPoints.size()
    ) {
        return false;
    }

    if (
        input.sourcePoints.size()
        != input.originalIndices.size()
    ) {
        return false;
    }

    if (
        input.sourcePoints.size()
        < static_cast<std::size_t>(
            params.minMatches
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 3. ALGORITHME RANSAC
    //
    // source      = B
    // destination = A
    //
    // Transformation cherchée :
    //
    // B -> A
    // --------------------------------------------------------

    cv::Mat masqueInliers;
    // [AD 21/10/2026 20:41] Réinitialisation de la graine du générateur pseudo-aléatoire OpenCV :
    // garantit la reproductibilité stricte (déterminisme § 4.4 du cahier des charges) pour RANSAC.
    cv::theRNG().state = 0xffffffff;

    cv::Mat affine =
        cv::estimateAffinePartial2D(
            input.sourcePoints,
            input.destinationPoints,
            masqueInliers,
            cv::RANSAC,
            params.reprojectionThreshold,
            params.maxIters,
            params.confidence,
            params.refineIters
        );

    // --------------------------------------------------------
    // 4. Vérification du résultat
    // --------------------------------------------------------

    if (
        affine.empty()
        || masqueInliers.empty()
    ) {
        return false;
    }

    if (
        masqueInliers.total()
        != input.sourcePoints.size()
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 5. Récupération des inliers
    //
    // On remet les résultats aux indices correspondant
    // aux Matches d'origine.
    // --------------------------------------------------------

    cv::Mat masquePlat =
        masqueInliers.reshape(1, 1);

    const uchar* valeurs =
        masquePlat.ptr<uchar>(0);

    for (
        std::size_t i = 0;
        i < input.sourcePoints.size();
        ++i
    ) {
        if (valeurs[i] == 0) {
            continue;
        }

        const std::size_t originalIndex =
            input.originalIndices[i];

        output.inliers[
            originalIndex
        ] = 1;

        ++output.inlierCount;
    }

    // --------------------------------------------------------
    // 6. Nombre minimum d'inliers
    // --------------------------------------------------------

    if (
        output.inlierCount
        < params.minInliers
    ) {
        output.inliers.assign(
            input.originalCount,
            0
        );

        output.inlierCount = 0;

        return false;
    }

    // --------------------------------------------------------
    // 7. Conversion de la matrice OpenCV
    //
    // estimateAffinePartial2D renvoie :
    //
    // [ a b tx ]
    // [ c d ty ]
    //
    // donc une matrice 2x3.
    //
    // Alignment attend une matrice homogène :
    //
    // [ a b tx ]
    // [ c d ty ]
    // [ 0 0  1 ]
    //
    // en CV_64F.
    // --------------------------------------------------------

    cv::Mat affine64;

    affine.convertTo(
        affine64,
        CV_64F
    );

    output.transform =
        cv::Mat::eye(
            3,
            3,
            CV_64F
        );

    affine64.copyTo(
        output.transform(
            cv::Rect(
                0,
                0,
                3,
                2
            )
        )
    );

    return true;
}

} // namespace stitching::rejection::ransac