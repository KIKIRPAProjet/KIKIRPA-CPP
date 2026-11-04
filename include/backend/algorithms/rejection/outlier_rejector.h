// include/backend/algorithms/rejection/outlier_rejector.h
#pragma once

#include "core/data/algorithm_types.h"

namespace RejectionNs {

    // À implémenter par chaque méthode de rejet des outliers (RANSAC, vote, MAGSAC++...).
    class OutlierRejectorClass {

        public:
            virtual ~OutlierRejectorClass() = default;

            // Estime la transformation de B vers A à partir des correspondances et marque les inliers. false si
            // l'estimation échoue.
            virtual bool estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) = 0;

    };

}
