// src/backend/steps/matching_step_utils.cpp
#include "steps/matching_step_utils.h"
#include "core/data/tile.h"
#include "core/data/algorithm_types.h"
#include "core/io/unicode_path.h"
#include "core/step/data_store.h"

#include <cstddef>
#include <nlohmann/json.hpp>

#include <opencv2/core/mat.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    std::vector<NeighborPair> collectPairs(const DataNs::TileList& tiles) {
        if (tiles.size() < 2) throw std::invalid_argument("at least two tiles are required");
        std::set<const DataNs::TileClass*> members;
        std::set<int> ids;
        for (const auto& tile : tiles) {
            if (!tile) throw std::invalid_argument("null tile");
            if (!members.insert(tile.get()).second || !ids.insert(tile->getId()).second)
                throw std::invalid_argument("duplicate tile or tile id");
            if (tile->getId() < 0) throw std::invalid_argument("negative tile id");
        }
        std::vector<NeighborPair> pairs;
        std::set<std::pair<int, int>> seen;
        for (const auto& tile : tiles) {
            const std::pair<const DataNs::TileClass*, DataNs::Direction> neighbors[] = {
                {tile->getRight(), DataNs::Direction::Right}, {tile->getBottom(), DataNs::Direction::Bottom}
            };
            for (const auto& neighbor : neighbors) {
                if (!neighbor.first) continue;
                if (neighbor.first == tile.get() || !members.count(neighbor.first))
                    throw std::invalid_argument("neighbor must be a distinct tile from the input DataNs::TileList");
                const auto edge = std::minmax({tile->getId(), neighbor.first->getId()});
                if (!seen.emplace(edge.first, edge.second).second)
                    throw std::invalid_argument("duplicate or cyclic neighbor pair");
                pairs.push_back({tile.get(), neighbor.first, neighbor.second});
            }
        }
        if (pairs.empty()) throw std::invalid_argument("no right or bottom neighbor pairs");
        return pairs;
    }

    cv::Mat readImage(const DataNs::TileClass& tile) {
        auto image = IONs::readImage(tile.getSource(), cv::IMREAD_UNCHANGED);
        if (image.empty()) throw std::runtime_error("cannot read " + IONs::pathToUtf8(tile.getSource()));
        if (image.dims != 2 || (image.depth() != CV_8U && image.depth() != CV_16U) ||
            (image.channels() != 1 && image.channels() != 3 && image.channels() != 4))
            throw std::invalid_argument("expected a 2D 8/16-bit image with 1, 3 or 4 channels");
        return image;
    }

    void validateMatches(const DataNs::Matches& matches, cv::Size imageSizeA, cv::Size imageSizeB, int minimumMatchCount) {
        const auto matchCount = matches.pointsInA.size();
        if (matchCount != matches.pointsInB.size() || matchCount != matches.confidenceScores.size() || matchCount < static_cast<size_t>(minimumMatchCount))
            throw std::runtime_error("insufficient or inconsistent correspondences");
        const auto isValidPoint = [](cv::Point2f point, cv::Size imageSize) {
            return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0.f && point.y >= 0.f &&
                   point.x < static_cast<float>(imageSize.width) && point.y < static_cast<float>(imageSize.height);
        };
        for (size_t i = 0; i < matchCount; ++i)
            if (!isValidPoint(matches.pointsInA[i], imageSizeA) || !isValidPoint(matches.pointsInB[i], imageSizeB) ||
                !std::isfinite(matches.confidenceScores[i]) || matches.confidenceScores[i] < 0.f || matches.confidenceScores[i] > 1.f)
                throw std::runtime_error("invalid point or confidence score (expected 0 to 1)");
        if (!std::isfinite(matches.translationX) || !std::isfinite(matches.translationY))
            throw std::runtime_error("non-finite translation");
    }

    void restoreCoordinates(DataNs::Matches& matches, const cv::Rect& regionA, const cv::Rect& regionB) {
        for (auto& point : matches.pointsInA) {
            point.x += static_cast<float>(regionA.x);
            point.y += static_cast<float>(regionA.y);
        }
        for (auto& point : matches.pointsInB) {
            point.x += static_cast<float>(regionB.x);
            point.y += static_cast<float>(regionB.y);
        }
        matches.translationX += static_cast<float>(regionA.x - regionB.x);
        matches.translationY += static_cast<float>(regionA.y - regionB.y);
    }

    const DataNs::OverlapEstimates* findOverlapEstimates(const StepNs::DataStoreClass& dataStore) {
        return dataStore.has("overlapEstimates") ? &dataStore.get<DataNs::OverlapEstimates>("overlapEstimates") : nullptr;
    }

    std::optional<cv::Point2d> findEstimatedTranslation(const DataNs::OverlapEstimates* estimates, int tileIdA, int tileIdB) {
        if (estimates == nullptr) return std::nullopt;
        for (const DataNs::PairOverlapEstimate& pairEstimate : *estimates)
            if (pairEstimate.tileIdA == tileIdA && pairEstimate.tileIdB == tileIdB) return pairEstimate.estimate.translationBInA;
        return std::nullopt;
    }

    float overlapForDirection(DataNs::Direction direction, float overlap, float verticalOverlap) {
        const bool isVertical = direction == DataNs::Direction::Top || direction == DataNs::Direction::Bottom;
        return isVertical && verticalOverlap > 0.f ? verticalOverlap : overlap;
    }

    std::string checkOverlaps(float overlap, float verticalOverlap) {
        if (!std::isfinite(overlap) || !(overlap > 0.f && overlap < 1.f))
            return "overlap must be in ]0, 1[, got " + std::to_string(overlap);
        if (!std::isfinite(verticalOverlap) || verticalOverlap < 0.f || verticalOverlap >= 1.f)
            return "verticalOverlap must be 0 (same as overlap) or in ]0, 1[, got " + std::to_string(verticalOverlap);
        return {};
    }

    double measuredOverlapFraction(DataNs::Direction direction, cv::Size tileSize, cv::Point2d translation) {
        const bool isVertical = direction == DataNs::Direction::Top || direction == DataNs::Direction::Bottom;
        const double tileLength = isVertical ? tileSize.height : tileSize.width;
        const double shift = isVertical ? translation.y : translation.x;
        return (tileLength - std::abs(shift)) / tileLength;
    }

    std::optional<double> calibrateOverlap(const std::vector<OverlapObservation>& observations, DataNs::Direction direction) {
        const auto isSameAxis = [direction](DataNs::Direction other) {
            const auto isVertical = [](DataNs::Direction value) {
                return value == DataNs::Direction::Top || value == DataNs::Direction::Bottom;
            };
            return isVertical(other) == isVertical(direction);
        };
        std::vector<double> fractions;
        bool hasOutOfMarginPeak = false;
        for (const OverlapObservation& observation : observations) {
            if (!isSameAxis(observation.direction) || !std::isfinite(observation.overlapFraction)) continue;
            fractions.push_back(observation.overlapFraction);
            hasOutOfMarginPeak = hasOutOfMarginPeak || observation.isOutOfMargin;
        }
        if (!hasOutOfMarginPeak || fractions.size() < static_cast<std::size_t>(MINIMUM_AGREEING_OBSERVATIONS))
            return std::nullopt;

        std::vector<double> sorted = fractions;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t middle = sorted.size() / 2;
        const double median = sorted.size() % 2 == 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
        const auto agreeing = std::count_if(fractions.begin(), fractions.end(), [median](double fraction) {
            return std::abs(fraction - median) <= OVERLAP_AGREEMENT_TOLERANCE;
        });
        if (agreeing < MINIMUM_AGREEING_OBSERVATIONS || 2 * static_cast<std::size_t>(agreeing) <= fractions.size())
            return std::nullopt;
        if (!(median > 0 && median < 1)) return std::nullopt;
        return median;
    }

    nlohmann::json serializeOutput(const std::string& key, const std::vector<DataNs::PairMatches>& value, const std::string& type) {
        nlohmann::json json;
        nlohmann::json pairs = nlohmann::json::array();
        for (const auto& pair : value) {
            if (pair.direction != DataNs::Direction::Right && pair.direction != DataNs::Direction::Bottom)
                throw std::invalid_argument("matching output expects right or bottom pairs");
            nlohmann::json pointsInAJson = nlohmann::json::array(), pointsInBJson = nlohmann::json::array();
            for (const auto& point : pair.featureMatches.pointsInA) pointsInAJson.push_back({point.x, point.y});
            for (const auto& point : pair.featureMatches.pointsInB) pointsInBJson.push_back({point.x, point.y});
            pairs.push_back({{"tileIdA", pair.tileIdA}, {"tileIdB", pair.tileIdB},
                             {"direction", pair.direction == DataNs::Direction::Right ? "right" : "bottom"},
                             {"matches", {{"pointsInA", std::move(pointsInAJson)}, {"pointsInB", std::move(pointsInBJson)},
                                          {"confidenceScores", pair.featureMatches.confidenceScores}, {"translationX", pair.featureMatches.translationX}, {"translationY", pair.featureMatches.translationY}}}});
        }
        json = {{"key", key}, {"value", std::move(pairs)}, {"type", type}};
        return json;
    }

}
