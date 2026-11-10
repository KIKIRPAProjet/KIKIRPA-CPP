// include/backend/core/data/pair_residual.h
#pragma once

#include <opencv2/core/types.hpp>

namespace DataNs {

    // Écart d'une paire après l'ajustement global (absolute-pos) : translation mesurée − (position B − position A)
    // ajustées, en pixels de pleine résolution.
    struct PairResidual {
        int tileIdA = 0;              // TileClass::getId() de A et de B, comme PairMatches
        int tileIdB = 0;
        cv::Point2d residual;         // (rx, ry)
        double residualNorm = 0.0;    // norme de residual
    };

}
