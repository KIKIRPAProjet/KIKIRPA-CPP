#include "algorithms/rejection/ransac/RansacRejector.h"

#include "algorithms/rejection/ransac/RansacAlgorithm.h"
#include "algorithms/rejection/ransac/RansacPreprocessor.h"
#include "algorithms/rejection/ransac/RansacTypes.h"

#include <algorithm>
#include <utility>

namespace stitching::rejection {

namespace {

ransac::RansacParameters makeParameters(
    const Json& params
)
{
    ransac::RansacParameters result;

    // --------------------------------------------------------
    // Nombre minimum de matches
    // --------------------------------------------------------

    result.minMatches =
        std::max(
            2,
            params.value(
                "minMatches",
                4
            )
        );

    // --------------------------------------------------------
    // Seuil de reprojection RANSAC
    // --------------------------------------------------------

    result.reprojectionThreshold =
        std::max(
            0.000001,
            params.value(
                "ransacReprojThreshold",
                3.0
            )
        );

    // --------------------------------------------------------
    // Nombre maximum d'itérations
    // --------------------------------------------------------

    const int maxIters =
        params.value(
            "maxIters",
            2000
        );

    result.maxIters =
        static_cast<std::size_t>(
            std::max(
                1,
                maxIters
            )
        );

    // --------------------------------------------------------
    // Niveau de confiance
    // --------------------------------------------------------

    result.confidence =
        std::clamp(
            params.value(
                "confidence",
                0.99
            ),
            0.000001,
            0.999999
        );

    // --------------------------------------------------------
    // Raffinement final
    // --------------------------------------------------------

    const int refineIters =
        params.value(
            "refineIters",
            10
        );

    result.refineIters =
        static_cast<std::size_t>(
            std::max(
                0,
                refineIters
            )
        );

    // --------------------------------------------------------
    // Nombre minimum d'inliers
    // --------------------------------------------------------

    result.minInliers =
        std::max(
            2,
            params.value(
                "minInliers",
                6
            )
        );

    return result;
}

} // namespace


RansacRejector::RansacRejector(
    const Json& p
)
    : params(p)
{
}


bool RansacRejector::estimate(
    const Matches& matches,
    Alignment& out
)
{
    // --------------------------------------------------------
    // 1. Nettoyage de la sortie du pipeline
    // --------------------------------------------------------

    out.transform.release();
    out.inliers.clear();

    // --------------------------------------------------------
    // 2. Conversion des paramètres JSON vers une structure
    // propre à RANSAC.
    // --------------------------------------------------------

    const ransac::RansacParameters settings =
        makeParameters(params);

    // --------------------------------------------------------
    // 3. STRUCTURE D'ENTREE
    //
    // On cherche une transformation :
    //
    // B -> A
    //
    // source      = pointsB
    // destination = pointsA
    // --------------------------------------------------------

    const ransac::RansacInput input{
        matches.pointsB,
        matches.pointsA
    };

    // --------------------------------------------------------
    // 4. PRETRAITEMENT
    // --------------------------------------------------------

    ransac::RansacPreprocessor preprocessor;

    ransac::RansacPreparedInput preparedInput;

    if (
        !preprocessor.prepare(
            input,
            settings,
            preparedInput
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 5. ALGORITHME RANSAC
    // --------------------------------------------------------

    ransac::RansacAlgorithm algorithm;

    ransac::RansacOutput result;

    if (
        !algorithm.estimate(
            preparedInput,
            settings,
            result
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 6. Conversion de la sortie interne RANSAC
    // vers la sortie générique du projet.
    // --------------------------------------------------------

    out.transform =
        result.transform.clone();

    out.inliers =
        std::move(
            result.inliers
        );

    return true;
}

} // namespace stitching::rejection