#pragma once
#include "algorithms/matching/FeatureMatcher.h"
#include "algorithms/rejection/OutlierRejector.h"
#include "algorithms/seamline/SeamFinder.h"
#include <memory>
#include <string>
#include <vector>

namespace stitching {

// Crée un algorithme à partir de son nom (ex. "sift", "ransac", "dynprog").
// Lève std::invalid_argument si le nom est inconnu.
std::unique_ptr<matching::FeatureMatcher>   makeFeatureMatcher(const std::string& name, const Json& params = {});
std::unique_ptr<rejection::OutlierRejector> makeOutlierRejector(const std::string& name, const Json& params = {});
std::unique_ptr<seamline::SeamFinder>       makeSeamFinder(const std::string& name, const Json& params = {});

// Noms enregistrés (utilisés par la GUI pour remplir les listes)
std::vector<std::string> featureMatcherNames();
std::vector<std::string> outlierRejectorNames();
std::vector<std::string> seamFinderNames();

} // namespace stitching
