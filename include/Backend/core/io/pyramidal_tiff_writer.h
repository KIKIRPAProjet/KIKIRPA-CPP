// include/Backend/core/io/pyramidal_tiff_writer.h
#pragma once

#include <opencv2/core/mat.hpp>

#include <filesystem>
#include <string>

namespace IONs {

    bool createPyramidalTiledTiff(
        const cv::Mat& image,
        const std::filesystem::path& outputPath,
        std::string& errorMessage
    );

}
