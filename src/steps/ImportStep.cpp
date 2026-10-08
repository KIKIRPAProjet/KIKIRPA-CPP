#include "steps/ImportStep.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <string>
#include <vector>

#ifndef STITCHING_PROJECT_ROOT
#define STITCHING_PROJECT_ROOT "."
#endif

namespace stitching::steps {

ImportStep::ImportStep(std::filesystem::path p, std::regex r, Tuile& a, Tuile& b)
    : path(std::move(p)), pattern(std::move(r)), first(a), second(b) {}

bool ImportStep::execute() {
    std::filesystem::path imageDirectory = path;
    if (!imageDirectory.is_absolute() &&
        !std::filesystem::is_directory(imageDirectory)) {
        const auto projectDirectory =
            std::filesystem::path(STITCHING_PROJECT_ROOT) / imageDirectory;
        if (std::filesystem::is_directory(projectDirectory)) {
            imageDirectory = projectDirectory;
        }
    }

    if (!std::filesystem::is_directory(imageDirectory)) {
        outputData["error"] =
            "Image directory not found: " + path.string() +
            " (working directory: " +
            std::filesystem::current_path().string() + ")";
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(imageDirectory)) {
        if (!entry.is_regular_file()) continue;

        const auto filename = entry.path().filename().string();
        if (std::regex_match(filename, pattern)) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    if (files.size() < 2) {
        outputData["error"] =
            "Expected at least two matching images in " + imageDirectory.string();
        outputData["found"] = files.size();
        return false;
    }

    const cv::Mat image1 = cv::imread(files[0].string(), cv::IMREAD_UNCHANGED);
    const cv::Mat image2 = cv::imread(files[1].string(), cv::IMREAD_UNCHANGED);
    if (image1.empty() || image2.empty()) {
        outputData["error"] = "Could not read the first two matching images";
        return false;
    }

    first.getPyramide().original = {{image1}};
    second.getPyramide().original = {{image2}};
    outputData["image1"] = files[0].string();
    outputData["image2"] = files[1].string();
    outputData["size1"] = {image1.cols, image1.rows};
    outputData["size2"] = {image2.cols, image2.rows};
    return true;
}

} // namespace stitching::steps
