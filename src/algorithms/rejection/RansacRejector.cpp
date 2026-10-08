#include "algorithms/rejection/RansacRejector.h"

#include <opencv2/calib3d.hpp>

namespace stitching::rejection {

RansacRejector::RansacRejector(const Json& p)
    : params(p)
{
}

bool RansacRejector::estimate(
    const Matches& matches,
    Alignment& out
)
{
    // Nettoyage de la sortie
    out.transform.release();
    out.inliers.clear();

    // Les deux listes doivent avoir la meme taille
    if (matches.pointsA.size() != matches.pointsB.size()) {
        return false;
    }

    // Parametres configurables
    const int minMatches =
        params.value("minMatches", 4);

    const double ransacThreshold =
        params.value("ransacReprojThreshold", 3.0);

    const std::size_t maxIters =
        params.value("maxIters", static_cast<std::size_t>(2000));

    const double confidence =
        params.value("confidence", 0.99);

    const std::size_t refineIters =
        params.value("refineIters", static_cast<std::size_t>(10));

    const int minInliers =
        params.value("minInliers", 6);

    // Pas assez de correspondances
    if (static_cast<int>(matches.pointsA.size()) < minMatches) {
        return false;
    }

    // --------------------------------------------------------
    // RANSAC
    //
    // On cherche une transformation B -> A
    //
    // source      = pointsB
    // destination = pointsA
    // --------------------------------------------------------

    cv::Mat masqueInliers;

    cv::Mat affine = cv::estimateAffinePartial2D(
        matches.pointsB,
        matches.pointsA,
        masqueInliers,
        cv::RANSAC,
        ransacThreshold,
        maxIters,
        confidence,
        refineIters
    );

    // Echec de l'estimation
    if (affine.empty() || masqueInliers.empty()) {
        return false;
    }

    // --------------------------------------------------------
    // Recuperation des inliers
    // --------------------------------------------------------

    out.inliers.resize(
        matches.pointsA.size(),
        0
    );

    int nombreInliers = 0;

    for (int i = 0; i < masqueInliers.rows; ++i) {
        const uchar valeur =
            masqueInliers.at<uchar>(i, 0);

        out.inliers[
            static_cast<std::size_t>(i)
        ] = valeur;

        if (valeur != 0) {
            ++nombreInliers;
        }
    }

    // Pas assez d'inliers fiables
    if (nombreInliers < minInliers) {
        out.inliers.clear();
        return false;
    }

    // --------------------------------------------------------
    // Conversion de la matrice 2x3 vers 3x3 CV_64F
    //
    // Types.h exige :
    // transform = transformation B -> A
    // en 3x3, CV_64F
    // --------------------------------------------------------

    cv::Mat affine64;
    affine.convertTo(
        affine64,
        CV_64F
    );

    out.transform = cv::Mat::eye(
        3,
        3,
        CV_64F
    );

    affine64.copyTo(
        out.transform(
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

} // namespace stitching::rejection