#include "steps/AbsolutePositioningStep.h"
#include "core/Config.h"
#include <queue>

namespace stitching::steps {

AbsolutePositioningStep::AbsolutePositioningStep(const AbsolutePositioningInput& input) {
    StepConfig cfg;
    cfg.name = "absolutePositioning";
    cfg.options = {{"int", "referenceTile", input.referenceTile}};
    cfg.pipelineOptions = {{"Tiles", keys::Tiles}, {"Alignments", keys::Alignments}};
    cfg.signature.pipelineOptions = {{"Positions", keys::Positions}};
    config = cfg;
}

bool AbsolutePositioningStep::execute() {
    const Tiles& tiles = data->get<Tiles>(keys::Tiles);
    const Alignments& alignments = data->get<Alignments>(keys::Alignments);
    const int reference = option("referenceTile").get<int>();
    if (reference < 0 || static_cast<std::size_t>(reference) >= tiles.size()) {
        outputData["error"] = "referenceTile is outside the tile array";
        return false;
    }

    std::vector<std::vector<std::pair<std::size_t, cv::Matx33d>>> graph(tiles.size());
    for (const PairAlignment& alignment : alignments) {
        if (alignment.pair.first >= tiles.size() || alignment.pair.second >= tiles.size()) {
            outputData["error"] = "alignment refers to a tile index outside the tile array";
            return false;
        }
        graph[alignment.pair.first].push_back({alignment.pair.second, alignment.secondToFirst});
        graph[alignment.pair.second].push_back(
            {alignment.pair.first, alignment.secondToFirst.inv(cv::DECOMP_LU)});
    }

    std::vector<cv::Matx33d> transforms(tiles.size(), cv::Matx33d::eye());
    std::vector<bool> positioned(tiles.size(), false);
    std::queue<std::size_t> pending;
    positioned[static_cast<std::size_t>(reference)] = true;
    pending.push(static_cast<std::size_t>(reference));
    while (!pending.empty()) {
        const std::size_t current = pending.front();
        pending.pop();
        for (const auto& edge : graph[current]) {
            if (positioned[edge.first]) continue;
            transforms[edge.first] = transforms[current] * edge.second;
            positioned[edge.first] = true;
            pending.push(edge.first);
        }
    }
    for (std::size_t i = 0; i < positioned.size(); ++i) {
        if (!positioned[i]) {
            outputData["error"] = "tile " + std::to_string(i) + " is disconnected from referenceTile";
            return false;
        }
    }

    Positions positions;
    positions.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i)
        positions.push_back({i, transforms[i]});
    data->set(keys::Positions, std::move(positions));
    outputData["tiles"] = tiles.size();
    return true;
}

} // namespace stitching::steps
