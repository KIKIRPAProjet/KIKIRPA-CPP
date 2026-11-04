// include/backend/algorithms/rejection/ransac/ransac_rejector.h
#pragma once

#include "algorithms/rejection/outlier_rejector.h"
#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

namespace RansacNs {

    using Json = nlohmann::json;

    // Rejet des aberrants par RANSAC (similitude partielle B -> A).
    // Réglages (JSON, valeur par défaut si absent) : minMatches (4), ransacReprojThreshold (3.0), maxIters (2000),
    //   confidence (0.99), refineIters (10), minInliers (6).
    class RansacRejectorClass : public RejectionNs::OutlierRejectorClass {

        public:
            explicit RansacRejectorClass(const Json& initialParameters = {});

            bool estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) override;

        private:
            Json parameters;

    };

    // VoteRejectorClass est implémenté dans son module dédié (vote_2d/vote_rejector.h).
    // L'alias ci-dessous maintient la compatibilité ascendante avec l'espace de noms RansacNs.
    using VoteRejectorClass = RejectionNs::VoteRejectorClass;
    using VoteRejector = RejectionNs::VoteRejectorClass;

}
