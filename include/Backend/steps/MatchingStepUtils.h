#pragma once
#include "core/Tuile.h"
#include "core/Types.h"

namespace stitching::steps::matching_step {

struct NeighborPair { const Tuile* a; const Tuile* b; Direction direction; };
std::vector<NeighborPair> collectPairs(const Tiles& tiles);
cv::Mat readImage(const Tuile& tile);
void validateMatches(const Matches& matches, cv::Size a, cv::Size b, int minimum);
void restoreCoordinates(Matches& matches, const cv::Rect& a, const cv::Rect& b);
Json serializeOutput(const std::string& key, const std::vector<PairMatches>& value, const std::string& type);

} // namespace stitching::steps::matching_step
