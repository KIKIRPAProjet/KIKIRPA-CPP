#include "algorithms/rejection/MagsacRejector.h"

namespace stitching::rejection {

MagsacRejector::MagsacRejector(const Json& params) : params(params) {}

bool MagsacRejector::estimate(const Matches&, Alignment&) {
    // TODO : MAGSAC++
    return true;
}

} // namespace stitching::rejection
