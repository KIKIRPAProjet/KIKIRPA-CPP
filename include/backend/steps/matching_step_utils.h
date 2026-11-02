// include/backend/steps/matching_step_utils.h
#pragma once
#include <nlohmann/json.hpp>
#include "core/data/tile.h"
#include "core/data/algorithm_types.h"

namespace StepsNs::MatchingNs {

struct NeighborPair { const DataNs::TileClass* a; const DataNs::TileClass* b; DataNs::Direction direction; };
std::vector<NeighborPair> collectPairs(const DataNs::TileList& tiles);
cv::Mat readImage(const DataNs::TileClass& tile);
void validateMatches(const DataNs::Matches& matches, cv::Size a, cv::Size b, int minimum);
void restoreCoordinates(DataNs::Matches& matches, const cv::Rect& a, const cv::Rect& b);
nlohmann::json serializeOutput(const std::string& key, const std::vector<DataNs::PairMatches>& value, const std::string& type);

} // namespace StepsNs::MatchingNs
