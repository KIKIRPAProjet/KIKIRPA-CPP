// include/backend/algorithms/rejection/vote_2d/vote_rejector.h
#pragma once
#include "algorithms/rejection/OutlierRejector.h"

namespace stitching::rejection {

// Rejet des aberrants par Vote de Hough 2D et médiane itérative (pure translation 2D).
class VoteRejector : public OutlierRejector {
    Json parameters;
public:
    explicit VoteRejector(const Json& parameters = {});
    bool estimate(const Matches& matches, Alignment& out) override;
};

} // namespace stitching::rejection
