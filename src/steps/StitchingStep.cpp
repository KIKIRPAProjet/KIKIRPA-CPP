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

    outputData["x"] = matches.x;
    outputData["y"] = matches.y;
    outputData["matches"] = matches.pointsA.size();
    if (!alignment.transform.empty() && alignment.transform.type() == CV_64F) {
        std::vector<double> t;
        alignment.transform.reshape(1, 1).copyTo(t);
        outputData["transform"] = t;
    }
    return true;
}

} // namespace stitching::steps
