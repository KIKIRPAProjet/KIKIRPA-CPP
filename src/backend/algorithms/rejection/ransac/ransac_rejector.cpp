// src/backend/algorithms/rejection/ransac/ransac_rejector.cpp
#include "algorithms/rejection/ransac/ransac_rejector.h"

#include "algorithms/rejection/ransac/ransac_algorithm.h"
#include "algorithms/rejection/ransac/ransac_preprocessor.h"
#include "algorithms/rejection/ransac/ransac_types.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace RansacNs {

    namespace {

        // Conversion des paramètres JSON vers la structure propre à RANSAC, avec les bornes de chaque valeur.
        RansacParameters readParameters(const Json& parameters) {
            RansacParameters ransacParameters;
            // Nombre minimum de matches
            ransacParameters.minimumMatchCount = std::max(2, parameters.value("minMatches", 4));
            // Seuil de reprojection RANSAC
            ransacParameters.reprojectionThreshold = std::max(0.000001, parameters.value("ransacReprojThreshold", 3.0));
            // Nombre maximum d'itérations
            const int maximumIterationCount = parameters.value("maxIters", 2000);
            ransacParameters.maximumIterationCount = static_cast<std::size_t>(std::max(1, maximumIterationCount));
            // Niveau de confiance
            ransacParameters.confidence = std::clamp(parameters.value("confidence", 0.99), 0.000001, 0.999999);
            // Raffinement final
            const int refinementIterationCount = parameters.value("refineIters", 10);
            ransacParameters.refinementIterationCount = static_cast<std::size_t>(std::max(0, refinementIterationCount));
            // Nombre minimum d'inliers
            ransacParameters.minimumInlierCount = std::max(2, parameters.value("minInliers", 6));
            return ransacParameters;
        }

    }

    RansacRejectorClass::RansacRejectorClass(const Json& initialParameters)
        : parameters(initialParameters.is_object() ? initialParameters : Json::object()) {}

    bool RansacRejectorClass::estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) {
        // 1. Nettoyage de la sortie du pipeline
        alignment.transformBToA.release();
        alignment.inlierMask.clear();

        // 2. Conversion des paramètres JSON vers une structure propre à RANSAC
        const RansacParameters ransacParameters = readParameters(parameters);

        // 3. Structure d'entrée. On cherche une transformation B -> A : source = pointsInB, destination = pointsInA.
        const RansacInput input{matches.pointsInB, matches.pointsInA};

        // 4. Prétraitement
        RansacPreprocessorClass preprocessor;
        RansacPreparedInput preparedInput;
        if (!preprocessor.prepare(input, ransacParameters, preparedInput)) return false;

        // 5. Algorithme RANSAC
        RansacAlgorithmClass algorithm;
        RansacOutput ransacOutput;
        if (!algorithm.estimate(preparedInput, ransacParameters, ransacOutput)) return false;

        // 6. Conversion de la sortie interne RANSAC vers la sortie générique du projet
        alignment.transformBToA = ransacOutput.transformBToA.clone();
        alignment.inlierMask = std::move(ransacOutput.inlierMask);
        return true;
    }

}
