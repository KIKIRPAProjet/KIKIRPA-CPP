// include/backend/algorithms/stitching/positioning/position_solver.h
#pragma once

#include <opencv2/core/types.hpp>

#include <cstddef>
#include <vector>

namespace PositioningNs {

    struct RelativeTilePosition {
        std::size_t tileIndexA = 0;
        std::size_t tileIndexB = 0;
        cv::Point2d translation;
        double rotationRadians = 0.0;
    };

    struct PositionSolution {
        std::vector<bool> connected;
        std::vector<cv::Point2d> positions;
        std::vector<double> rotations;
    };

    // Ajuste toutes les contraintes B - A simultanément, avec la référence fixée à (0, 0, 0).
    // Les composantes sans lien avec cette référence ne sont pas placées (connected = false).
    // Lève une exception si les indices/mesures sont invalides ou si la résolution échoue.
    PositionSolution solveRelativePositions(std::size_t tileCount, const std::vector<RelativeTilePosition>& offsets,
                                           std::size_t referenceIndex = 0);

}
