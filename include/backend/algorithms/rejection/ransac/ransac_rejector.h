// include/backend/algorithms/rejection/ransac/ransac_rejector.h
#pragma once

#include "algorithms/rejection/outlier_rejector.h"
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

    // Rejet des aberrants par vote de Hough 2D + médiane itérative (Section 7 des notebooks KIK-IRPA).
    // Conçu pour les déplacements sur banc rigide (pure translation 2D) : insensible au bruit transversal
    // sur bande étroite et exempt de l'effet bras de levier qui crée de fausses rotations dans RANSAC 4-DDL.
    // Implémente OutlierRejectorClass en conservant RansacRejectorClass intact.
    // Réglages (JSON) : ceux de RansacRejectorClass, avec minMatches et minInliers à 3 par défaut, plus seuil (rayon
    //   des inliers en px, 1.5 par défaut ou ransacReprojThreshold) et case (taille de cellule de l'accumulateur, 1 px).
    class VoteRejectorClass : public RejectionNs::OutlierRejectorClass {

        public:
            explicit VoteRejectorClass(const Json& initialParameters = {});

            bool estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) override;

        private:
            Json parameters;

    };

}
