#include "steps/SeamlineStep.h"

namespace stitching::steps {

SeamlineStep::SeamlineStep(Tuile& a, Tuile& b, PairContext ctx,
                           std::unique_ptr<seamline::SeamFinder> finder)
    : a(a), b(b), ctx(ctx), finder(std::move(finder)) {}

bool SeamlineStep::execute() {
    if (!finder->find(a.image(), b.image(), ctx, seamMask)) return false;
    outputData["seams"] = !seamMask.empty();
    return true;
}

} // namespace stitching::steps
