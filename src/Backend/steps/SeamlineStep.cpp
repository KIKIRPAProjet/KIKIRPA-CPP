#include "steps/SeamlineStep.h"
#include "core/Config.h"

namespace stitching::steps {

SeamlineStep::SeamlineStep(const SeamlineInput& input) {
    StepConfig cfg;
    cfg.name = "seamline";
    cfg.options = {{"string", "seamFinder", input.seamFinder}};
    cfg.pipelineOptions = {{"Tiles", keys::Tiles}, {"Positions", keys::Positions}};
    cfg.signature.pipelineOptions = {{"Territories", keys::Territories}};
    config = cfg;
}

bool SeamlineStep::execute() {
    const Tiles& tiles = data->get<Tiles>(keys::Tiles);
    const Positions& positions = data->get<Positions>(keys::Positions);
    const std::string seamFinder = option("seamFinder").get<std::string>();
    if (seamFinder != "none") {
        outputData["error"] = "seamFinder currently supports only 'none'";
        return false;
    }
    if (positions.size() != tiles.size()) {
        outputData["error"] = "positions count does not match tile count";
        return false;
    }

    Territories territories;
    territories.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const cv::Mat& image = tiles[i].image;
        if (image.empty()) {
            outputData["error"] = "tile image is empty: " + tiles[i].path.string();
            return false;
        }
        cv::Mat mask(image.rows, image.cols, CV_8U, cv::Scalar(255));
        if (image.channels() == 4) {
            cv::Mat alpha;
            cv::extractChannel(image, alpha, 3);
            cv::compare(alpha, 0, mask, cv::CMP_GT);
        }
        territories.push_back({i, std::move(mask)});
    }
    data->set(keys::Territories, std::move(territories));
    outputData["territories"] = tiles.size();
    return true;
}

} // namespace stitching::steps
