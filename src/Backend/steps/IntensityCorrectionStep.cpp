#include "steps/IntensityCorrectionStep.h"
#include "core/Config.h"
#include <algorithm>

namespace stitching::steps {

IntensityCorrectionStep::IntensityCorrectionStep(const IntensityCorrectionInput& input) {
    StepConfig cfg;
    cfg.name = "intensityCorrection";
    cfg.options = {{"bool", "enabled", input.enabled}};
     cfg.pipelineOptions = {{"Tiles", keys::Tiles}};
    cfg.signature.pipelineOptions = {{"LightCorrections", keys::Corrections}};
    config = cfg;
}

bool IntensityCorrectionStep::execute() {
    const Tiles& tiles = data->get<Tiles>(keys::Tiles);
    const bool enabled = option("enabled").get<bool>();
    if (tiles.empty()) {
        outputData["error"] = "cannot correct an empty tile set";
        return false;
    }

    std::vector<cv::Scalar> means;
    means.reserve(tiles.size());
    cv::Scalar target(0, 0, 0, 0);
    for (const TileImage& tile : tiles) {
        if (tile.image.empty()) {
            outputData["error"] = "tile image is empty: " + tile.path.string();
            return false;
        }
        const cv::Scalar mean = cv::mean(tile.image);
        means.push_back(mean);
        for (int channel = 0; channel < tile.image.channels() && channel < 4; ++channel)
            target[channel] += mean[channel];
    }

    LightCorrections corrections(tiles.size());
    if (enabled) {
        const int channels = std::min(4, tiles.front().image.channels());
        for (int channel = 0; channel < channels; ++channel)
            target[channel] /= static_cast<double>(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            for (int channel = 0; channel < channels; ++channel) {
                if (channel == 3) continue;
                const double mean = means[i][channel];
                corrections[i].gain[channel] = mean > 1e-6
                    ? std::clamp(target[channel] / mean, 0.5, 2.0)
                    : 1.0;
            }
        }
    }
    data->set(keys::Corrections, std::move(corrections));
    outputData["tiles"] = tiles.size();
    return true;
}

} // namespace stitching::steps
