// src/backend/steps/matching_step_utils.cpp
#include "steps/matching_step_utils.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

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
        auto image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
        if (image.empty()) throw std::runtime_error("cannot read " + tile.getSource().string());
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
                   point.x < imageSize.width && point.y < imageSize.height;
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
