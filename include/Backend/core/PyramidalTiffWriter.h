#pragma once

#include <string>
#include <filesystem>
#include <opencv2/core/mat.hpp>

namespace stitching {

bool createPyramidalTiledTiff(
    const cv::Mat& image,
    const std::filesystem::path& outputPath,
    std::string& error
);

}