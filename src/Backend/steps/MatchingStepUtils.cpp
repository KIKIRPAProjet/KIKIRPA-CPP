#include "steps/MatchingStepUtils.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace stitching::steps::matching_step {
std::vector<NeighborPair> collectPairs(const Tiles& tiles) {
    if (tiles.size() < 2) throw std::invalid_argument("at least two tiles are required");
    std::set<const Tuile*> members;
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
        const std::pair<const Tuile*, Direction> neighbors[] = {
            {tile->getRight(), Direction::Right}, {tile->getBottom(), Direction::Bottom}
        };
        for (const auto& neighbor : neighbors) {
            if (!neighbor.first) continue;
            if (neighbor.first == tile.get() || !members.count(neighbor.first))
                throw std::invalid_argument("neighbor must be a distinct tile from the input Tiles");
            const auto edge = std::minmax({tile->getId(), neighbor.first->getId()});
            if (!seen.emplace(edge.first, edge.second).second)
                throw std::invalid_argument("duplicate or cyclic neighbor pair");
            pairs.push_back({tile.get(), neighbor.first, neighbor.second});
        }
    }
    if (pairs.empty()) throw std::invalid_argument("no right or bottom neighbor pairs");
    return pairs;
}

cv::Mat readImage(const Tuile& tile) {
    auto image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
    if (image.empty()) throw std::runtime_error("cannot read " + tile.getSource().string());
    if (image.dims != 2 || (image.depth() != CV_8U && image.depth() != CV_16U) ||
        (image.channels() != 1 && image.channels() != 3 && image.channels() != 4))
        throw std::invalid_argument("expected a 2D 8/16-bit image with 1, 3 or 4 channels");
    return image;
}


void validateMatches(const Matches& matches, cv::Size a, cv::Size b, int minimum) {
    const auto count = matches.pointsA.size();
    if (count != matches.pointsB.size() || count != matches.scores.size() || count < static_cast<size_t>(minimum))
        throw std::runtime_error("insufficient or inconsistent correspondences");
    const auto valid = [](cv::Point2f p, cv::Size size) {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.f && p.y >= 0.f &&
               p.x < size.width && p.y < size.height;
    };
    for (size_t i = 0; i < count; ++i)
        if (!valid(matches.pointsA[i], a) || !valid(matches.pointsB[i], b) ||
            !std::isfinite(matches.scores[i]) || matches.scores[i] < 0.f || matches.scores[i] > 1.f)
            throw std::runtime_error("invalid point or confidence score (expected 0 to 1)");
    if (!std::isfinite(matches.dx) || !std::isfinite(matches.dy))
        throw std::runtime_error("non-finite translation");
}

void restoreCoordinates(Matches& matches, const cv::Rect& a, const cv::Rect& b) {
    for (auto& p : matches.pointsA) { p.x += a.x; p.y += a.y; }
    for (auto& p : matches.pointsB) { p.x += b.x; p.y += b.y; }
    matches.dx += static_cast<float>(a.x - b.x);
    matches.dy += static_cast<float>(a.y - b.y);
}

Json serializeOutput(const std::string& key, const std::vector<PairMatches>& value, const std::string& type) {
    Json json;
    Json pairs = Json::array();
    for (const auto& pair : value) {
        if (pair.dir != Direction::Right && pair.dir != Direction::Bottom)
            throw std::invalid_argument("matching output expects right or bottom pairs");
        Json pointsA = Json::array(), pointsB = Json::array();
        for (const auto& point : pair.matches.pointsA) pointsA.push_back({point.x, point.y});
        for (const auto& point : pair.matches.pointsB) pointsB.push_back({point.x, point.y});
        pairs.push_back({{"idA", pair.idA}, {"idB", pair.idB},
                         {"direction", pair.dir == Direction::Right ? "right" : "bottom"},
                         {"matches", {{"pointsA", std::move(pointsA)}, {"pointsB", std::move(pointsB)},
                                      {"scores", pair.matches.scores}, {"dx", pair.matches.dx}, {"dy", pair.matches.dy}}}});
    }
    json = {{"key", key}, {"value", std::move(pairs)}, {"type", type}};
    return json;
}

} // namespace stitching::steps::matching_step
