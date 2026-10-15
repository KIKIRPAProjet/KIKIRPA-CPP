#pragma once
#include "algorithms/rejection/OutlierRejector.h"

namespace stitching::rejection {

class RansacRejector : public OutlierRejector {
    Json params;
public:
    explicit RansacRejector(const Json& params = {});
    bool estimate(const Matches& matches, Alignment& out) override;
};

} // namespace stitching::rejection
