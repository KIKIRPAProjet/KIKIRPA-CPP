#include "algorithms/rejection/ransac/RansacPreprocessor.h"

#include <cmath>

namespace stitching::rejection::ransac {

bool RansacPreprocessor::isFinite(
    const cv::Point2f& point
)
{
    return std::isfinite(point.x)
        && std::isfinite(point.y);
}

bool RansacPreprocessor::prepare(
    const RansacInput& input,
    const RansacParameters& params,
    RansacPreparedInput& output
) const
{
    // --------------------------------------------------------
    // 1. Nettoyage de la sortie
    // --------------------------------------------------------

    output.sourcePoints.clear();
    output.destinationPoints.clear();
    output.originalIndices.clear();

    output.originalCount =
        input.sourcePoints.size();

    // --------------------------------------------------------
    // 2. Cohérence des correspondances
    //
    // Chaque point de B doit correspondre à un point de A.
    // --------------------------------------------------------

    if (
        input.sourcePoints.size()
        != input.destinationPoints.size()
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 3. Nombre minimum de matches
    // --------------------------------------------------------

    if (
        input.sourcePoints.size()
        < static_cast<std::size_t>(
            params.minMatches
        )
    ) {
        return false;
    }

    // --------------------------------------------------------
    // 4. Réservation mémoire
    // --------------------------------------------------------

    output.sourcePoints.reserve(
        input.sourcePoints.size()
    );

    output.destinationPoints.reserve(
        input.destinationPoints.size()
    );

    output.originalIndices.reserve(
        input.sourcePoints.size()
    );

    // --------------------------------------------------------
    // 5. PRETRAITEMENT
    //
    // Pour l'instant :
    //
    // - vérification des coordonnées ;
    // - suppression des NaN ;
    // - suppression des infinis ;
    // - conservation de l'indice du match original.
    //
    // Aucun RANSAC n'est exécuté ici.
    // --------------------------------------------------------

    for (
        std::size_t i = 0;
        i < input.sourcePoints.size();
        ++i
    ) {
        const cv::Point2f& source =
            input.sourcePoints[i];

        const cv::Point2f& destination =
            input.destinationPoints[i];

        if (
            !isFinite(source)
            || !isFinite(destination)
        ) {
            continue;
        }

        output.sourcePoints.push_back(
            source
        );

        output.destinationPoints.push_back(
            destination
        );

        output.originalIndices.push_back(
            i
        );
    }

    // --------------------------------------------------------
    // 6. Vérification après le prétraitement
    // --------------------------------------------------------

    if (
        output.sourcePoints.size()
        < static_cast<std::size_t>(
            params.minMatches
        )
    ) {
        return false;
    }

    return true;
}

} // namespace stitching::rejection::ransac