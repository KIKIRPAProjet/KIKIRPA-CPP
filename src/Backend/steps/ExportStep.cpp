#include "steps/ExportStep.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>

namespace stitching::steps {

ExportStep::ExportStep(const ExportInput& input) {
    StepConfig cfg;
    cfg.name = "export";
    cfg.options = {{"string", "path", input.path}, {"int", "bits", input.bits}};
    cfg.pipelineOptions = {{"Mosaic", keys::Mosaic}};
    config = cfg;
}

bool ExportStep::execute() {
    const std::string path = option("path").get<std::string>();
    const int bits = option("bits").get<int>();
    if (path.empty()) {
        outputData["error"] = "path cannot be empty";
        return false;
    }
    if (bits != 8 && bits != 16) {
        outputData["error"] = "bits must be 8 or 16";
        return false;
    }

    const Mosaic& mosaic = data->get<Mosaic>(keys::Mosaic);
    if (mosaic.image.empty()) {
        outputData["error"] = "mosaic image is empty";
        return false;
    }
    try {
        const std::filesystem::path destination(path);
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());
        cv::Mat converted;
        const int depth = bits == 8 ? CV_8U : CV_16U;
        double scale = 1.0;
        if (depth == CV_8U && mosaic.sourceDepth == CV_16U) scale = 1.0 / 257.0;
        if (depth == CV_16U && mosaic.sourceDepth == CV_8U) scale = 257.0;
        mosaic.image.convertTo(converted, CV_MAKETYPE(depth, mosaic.image.channels()), scale);
        if (!cv::imwrite(destination.string(), converted)) {
            outputData["error"] = "image encoder rejected output path '" + path + "'";
            return false;
        }
    } catch (const std::exception& e) {
        outputData["error"] = "cannot export '" + path + "': " + e.what();
        return false;
    }
    outputData["path"] = path;
    outputData["width"] = mosaic.image.cols;
    outputData["height"] = mosaic.image.rows;
    return true;
}

} // namespace stitching::steps
