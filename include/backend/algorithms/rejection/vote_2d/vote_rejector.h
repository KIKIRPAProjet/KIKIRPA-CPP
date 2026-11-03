// include/backend/algorithms/rejection/vote_2d/vote_rejector.h
#pragma once
#include "algorithms/legacy_type_aliases.h"
#include "algorithms/rejection/OutlierRejector.h"
#include "core/data/algorithm_types.h"

// NOLINTBEGIN(misc-include-cleaner)
namespace stitching::rejection { // NOLINT(readability-identifier-naming)

// Rejet des aberrants par Vote de Hough 2D et médiane itérative (pure translation 2D).
class VoteRejectorClass : public OutlierRejector {
public:
    VoteRejectorClass() = default;
    explicit VoteRejectorClass(const Json& parameters);
    bool estimate(const Matches& matches, Alignment& out) override;

private:
    Json parameters = Json::object();
};

using VoteRejector = VoteRejectorClass;

} // namespace stitching::rejection
// NOLINTEND(misc-include-cleaner)

