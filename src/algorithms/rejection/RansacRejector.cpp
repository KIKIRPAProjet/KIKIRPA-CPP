#include "algorithms/rejection/RansacRejector.h"

namespace stitching::rejection {

RansacRejector::RansacRejector(const Json& params) : params(params) {}

bool RansacRejector::estimate(const Matches&, Alignment&) {
    // TODO : RANSAC
    return true;
}

} // namespace stitching::rejection
