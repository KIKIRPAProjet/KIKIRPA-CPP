#pragma once
#include "core/Pipeline.h"
#include "core/Tuile.h"
#include <filesystem>
#include <string>

namespace stitching {

struct PipelineSettings {
    std::filesystem::path imageDir = "images/";
    std::string pattern = ".*\\.png";
    Direction direction = Direction::LeftRight;
    float overlap = 0.2f;
    std::string matcher = "lightglue";
    // std::filesystem::path lightGlueModel = "models/superpoint_2048_lightglue_end2end.onnx";
    std::filesystem::path lightGlueModel = "models/disk_2048_lightglue_end2end.onnx";
    std::string rejector = "ransac";
    std::string seamFinder = "dynprog";
};

// Les tuiles doivent survivre au pipeline (les steps gardent des références).
// Lève une exception si un nom d'algorithme ou le pattern est invalide.
Pipeline buildPipeline(const PipelineSettings& settings, Tuile& a, Tuile& b);

} // namespace stitching
