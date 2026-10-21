#pragma once
#include "core/Pipeline.h"
#include "core/Tuile.h"
#include <filesystem>
#include <string>

namespace stitching {

struct PipelineSettings {
    std::filesystem::path imageDir = "images/";
    std::string format = R"(img_C(\d+)_R(\d+)\.tif)";   // voir steps::ImportInput
    Direction direction = Direction::LeftRight;
    float overlap = 0.2f;
    std::string matcher = "sift";
    std::string rejector = "ransac";
    std::string seamFinder = "dynprog";
    std::string modelPath;
    int minMatches = 4;
};

// Parcours import -> correspondances LightGlue (sans positionnement, fusion ni export).
Pipeline buildLightGluePipeline(const PipelineSettings& settings);

// Compatibilité avec l'ancienne signature ; seul matcher="lightglue" est implémenté.
Pipeline buildPipeline(const PipelineSettings& settings, Tuile& a, Tuile& b);

} // namespace stitching
