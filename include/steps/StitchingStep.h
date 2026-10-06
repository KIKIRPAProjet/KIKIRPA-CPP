#pragma once
#include "algorithms/matching/FeatureMatcher.h"
#include "algorithms/rejection/OutlierRejector.h"
#include "core/Step.h"
#include "core/Tuile.h"
#include <memory>

namespace stitching::steps {

// Aligne deux tuiles voisines : appariement puis rejet des outliers.
// Les algorithmes sont injectés (voir algorithms/Factory.h). Multi-threadable.
class StitchingStep : public Step {
    Tuile &a, &b;
    PairContext ctx;
    std::unique_ptr<matching::FeatureMatcher> matcher;
    std::unique_ptr<rejection::OutlierRejector> rejector;
public:
    StitchingStep(Tuile& a, Tuile& b, PairContext ctx,
                  std::unique_ptr<matching::FeatureMatcher> matcher,
                  std::unique_ptr<rejection::OutlierRejector> rejector);
    bool execute() override;
};

} // namespace stitching::steps
