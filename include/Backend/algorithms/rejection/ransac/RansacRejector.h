#pragma once
#include "algorithms/rejection/OutlierRejector.h"

namespace stitching::rejection {

class RansacRejector : public OutlierRejector {
    Json params;
public:
    explicit RansacRejector(const Json& params = {});
    bool estimate(const Matches& matches, Alignment& out) override;
};

// [AD 21/10/2026 20:41] Rejet des aberrants par Vote de Hough 2D + médiane itérative (Section 7 des notebooks KIK-IRPA).
// Conçu pour les déplacements sur banc rigide (pure translation 2D) : insensible au bruit transversal
// sur bande étroite et exempt de l'effet bras de levier qui crée de fausses rotations dans RANSAC 4-DDL.
// Implémente OutlierRejector en conservant RansacRejector 100% intact.
class VoteRejector : public OutlierRejector {
    Json params;
public:
    explicit VoteRejector(const Json& params = {});
    bool estimate(const Matches& matches, Alignment& out) override;
};

} // namespace stitching::rejection
