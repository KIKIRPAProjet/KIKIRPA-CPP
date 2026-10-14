#pragma once
#include "algorithms/rejection/OutlierRejector.h"

namespace stitching::rejection {

class MagsacRejector : public OutlierRejector {
    Json params;
public:
    explicit MagsacRejector(const Json& params = {});
    bool estimate(const Matches& matches, Alignment& out) override;
};

} // namespace stitching::rejection
