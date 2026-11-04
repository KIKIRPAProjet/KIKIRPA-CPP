// src/backend/algorithms/rejection/ransac/ransac_preprocessor.cpp
#include "algorithms/rejection/ransac/ransac_preprocessor.h"

#include <cmath>
#include <cstddef>

namespace RansacNs {

    namespace {

        bool isFinitePoint(const cv::Point2f& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

    }

    bool RansacPreprocessorClass::prepare(const RansacInput& input, const RansacParameters& parameters,
                                          RansacPreparedInput& output) const {
        // 1. Nettoyage de la sortie
        output.sourcePoints.clear();
        output.destinationPoints.clear();
        output.originalIndices.clear();
        output.originalCount = input.sourcePoints.size();

        // 2. Cohérence des correspondances : chaque point de B doit correspondre à un point de A.
        if (input.sourcePoints.size() != input.destinationPoints.size()) return false;

        // 3. Nombre minimum de matches
        if (input.sourcePoints.size() < static_cast<std::size_t>(parameters.minimumMatchCount)) return false;

        // 4. Réservation mémoire
        output.sourcePoints.reserve(input.sourcePoints.size());
        output.destinationPoints.reserve(input.destinationPoints.size());
        output.originalIndices.reserve(input.sourcePoints.size());

        // 5. Prétraitement. Pour l'instant : vérification des coordonnées, suppression des NaN et des infinis,
        // conservation de l'indice du match original. Aucun RANSAC n'est exécuté ici.
        for (std::size_t pointIndex = 0; pointIndex < input.sourcePoints.size(); ++pointIndex) {
            const cv::Point2f& source = input.sourcePoints[pointIndex];
            const cv::Point2f& destination = input.destinationPoints[pointIndex];
            if (!isFinitePoint(source) || !isFinitePoint(destination)) continue;
            output.sourcePoints.push_back(source);
            output.destinationPoints.push_back(destination);
            output.originalIndices.push_back(pointIndex);
        }

        // 6. Vérification après le prétraitement
        return output.sourcePoints.size() >= static_cast<std::size_t>(parameters.minimumMatchCount);
    }

}
