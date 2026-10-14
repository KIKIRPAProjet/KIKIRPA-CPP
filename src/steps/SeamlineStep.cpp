#include "steps/SeamlineStep.h"

namespace stitching::steps {

SeamlineStep::SeamlineStep(Tuile& a, Tuile& b, PairContext ctx,
                           std::unique_ptr<seamline::SeamFinder> finder)
    : a(a), b(b), ctx(ctx), finder(std::move(finder)) {}

bool SeamlineStep::execute() {
    return finder->find(a.image(), b.image(), ctx, seamMask);
}

} // namespace stitching::steps
