#pragma once
#include "algorithms/seamline/SeamFinder.h"
#include "core/Step.h"
#include "core/Tuile.h"
#include <memory>

namespace stitching::steps {

// Calcule la ligne de couture entre deux tuiles voisines.
// L'algorithme est injecté (voir algorithms/Factory.h).
class SeamlineStep : public Step {
    Tuile &a, &b;
    PairContext ctx;
    std::unique_ptr<seamline::SeamFinder> finder;
    cv::Mat seamMask;
public:
    SeamlineStep(Tuile& a, Tuile& b, PairContext ctx,
                 std::unique_ptr<seamline::SeamFinder> finder);
    bool execute() override;
    const cv::Mat& getSeamMask() const { return seamMask; }
};

} // namespace stitching::steps
