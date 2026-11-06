// include/backend/core/data/intensity_correction.h
#pragma once

#include <vector>

namespace DataNs {

    // Luminosité relative, dans l'ordre de TileList. La correction divise les pixels par le gain.
    // Le champ exp(c * r²) est également divisé ; les tuiles isolées restent non corrigées.
    struct IntensityCorrection {
        std::vector<double> tileGains;
        double fieldStrength = 0.0;
        std::vector<int> isolatedTileIndices;
    };

}
