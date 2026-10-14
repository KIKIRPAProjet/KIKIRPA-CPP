#include "core/Tuile.h"

namespace stitching {

Pyramide::Pyramide(Tuile* p) : parent(p) {
    half.parent = quarter.parent = eighth.parent = this;
}

cv::Mat Tuile::image() const {
    const Grid& g = pyramide->original;
    if (g.empty() || g.front().empty()) return {};
    return g.front().front();
}

} // namespace stitching
