#include "algorithms/Factory.h"
#include "algorithms/matching/HarrisNccMatcher.h"
#include "algorithms/matching/LightGlueMatcher.h"
#include "algorithms/matching/SiftMatcher.h"
#include "algorithms/rejection/MagsacRejector.h"
#include "algorithms/rejection/RansacRejector.h"
#include "algorithms/seamline/DynProgSeamFinder.h"
#include "algorithms/seamline/GraphCutPairSeamFinder.h"
#include <functional>
#include <map>
#include <stdexcept>

namespace stitching {
namespace {

template <class Base>
using Registry = std::map<std::string, std::function<std::unique_ptr<Base>(const Json&)>>;

template <class Base, class Impl>
std::unique_ptr<Base> make(const Json& params) { return std::make_unique<Impl>(params); }

template <class Base>
std::unique_ptr<Base> create(const Registry<Base>& registry, const std::string& kind,
                             const std::string& name, const Json& params) {
    auto it = registry.find(name);
    if (it == registry.end()) {
        std::string known;
        for (const auto& entry : registry) known += " " + entry.first;
        throw std::invalid_argument("Unknown " + kind + " '" + name + "' (available:" + known + ")");
    }
    return it->second(params);
}

template <class Base>
std::vector<std::string> namesOf(const Registry<Base>& registry) {
    std::vector<std::string> names;
    for (const auto& entry : registry) names.push_back(entry.first);
    return names;
}

// ---- Pour ajouter un algorithme : une ligne dans le registre correspondant ----

const Registry<matching::FeatureMatcher>& matchers() {
    using namespace matching;
    static const Registry<FeatureMatcher> registry = {
        {"sift",       make<FeatureMatcher, SiftMatcher>},
        {"harris-ncc", make<FeatureMatcher, HarrisNccMatcher>},
        {"lightglue",  make<FeatureMatcher, LightGlueMatcher>},
    };
    return registry;
}

const Registry<rejection::OutlierRejector>& rejectors() {
    using namespace rejection;
    static const Registry<OutlierRejector> registry = {
        {"ransac",   make<OutlierRejector, RansacRejector>},
        {"magsac++", make<OutlierRejector, MagsacRejector>},
    };
    return registry;
}

const Registry<seamline::SeamFinder>& seamFinders() {
    using namespace seamline;
    static const Registry<SeamFinder> registry = {
        {"dynprog",  make<SeamFinder, DynProgSeamFinder>},
        {"graphcut", make<SeamFinder, GraphCutPairSeamFinder>},
    };
    return registry;
}

} // namespace

std::unique_ptr<matching::FeatureMatcher> makeFeatureMatcher(const std::string& name, const Json& params) {
    return create(matchers(), "feature matcher", name, params);
}
std::unique_ptr<rejection::OutlierRejector> makeOutlierRejector(const std::string& name, const Json& params) {
    return create(rejectors(), "outlier rejector", name, params);
}
std::unique_ptr<seamline::SeamFinder> makeSeamFinder(const std::string& name, const Json& params) {
    return create(seamFinders(), "seam finder", name, params);
}

std::vector<std::string> featureMatcherNames()  { return namesOf(matchers()); }
std::vector<std::string> outlierRejectorNames() { return namesOf(rejectors()); }
std::vector<std::string> seamFinderNames()      { return namesOf(seamFinders()); }

} // namespace stitching
