#include "steps/StitchingStep.h"

namespace stitching::steps {

StitchingStep::StitchingStep(Tuile& a, Tuile& b, PairContext ctx,
                             std::unique_ptr<matching::FeatureMatcher> matcher,
                             std::unique_ptr<rejection::OutlierRejector> rejector)
    : a(a), b(b), ctx(ctx), matcher(std::move(matcher)), rejector(std::move(rejector)) {}

bool StitchingStep::execute() {
    Matches matches;
    if (!matcher->match(a.image(), b.image(), ctx, matches)) return false;

    Alignment alignment;
    if (!rejector->estimate(matches, alignment)) return false;

    return true;
}

} // namespace stitching::steps
