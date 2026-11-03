#pragma once
#include "core/Types.h"

namespace stitching::rejection {

// À implémenter par chaque méthode de rejet des outliers (RANSAC, MAGSAC++...)
class OutlierRejector {
public:
    virtual ~OutlierRejector() = default;
    virtual bool estimate(const Matches& matches, Alignment& out) = 0;
};

} // namespace stitching::rejection
