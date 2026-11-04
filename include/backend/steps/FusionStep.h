#pragma once

#include "core/step/step.h"
#include "core/data/tile.h"
#include "core/data/algorithm_types.h"
#include <opencv2/core.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace StepsNs {

struct FusionInput {
    int blockSize = 512;
    std::string outputPath = "fusion_output";
    bool enableBlending = false;
};

struct FusionOutput {
    std::string mosaicPath = ""; // Dossier contenant les blocs RGB, provenance et masque.
    int totalBlocks = 0;
};

class FusionStepClass : public StepNs::StepClass {
public:
    explicit FusionStepClass(const FusionInput& initialInput = {});
    ~FusionStepClass() override = default;
    bool execute() override;
    const FusionOutput& getOutput() const { return output_; }

private:
    FusionInput input_;
    FusionOutput output_;

    struct TilePlacement {
        int index = 0;
        std::filesystem::path path;
        cv::Rect globalBox;
        double lightGain = 1.0;
    };

    static cv::Rect computeGlobalBoundingBox(const std::vector<TilePlacement>& placements);
    static cv::Mat makeTerritoryMask(const cv::Rect& block,
                                     const DataNs::TileTerritory& territory);
    void renderBlock(const cv::Rect& block,
                     const std::vector<TilePlacement>& placements,
                     const std::vector<DataNs::TileTerritory>& territories,
                     cv::Mat& outBlockRgb,
                     cv::Mat& outBlockProvenance,
                     cv::Mat& outBlockMask) const;
};

} // namespace StepsNs
