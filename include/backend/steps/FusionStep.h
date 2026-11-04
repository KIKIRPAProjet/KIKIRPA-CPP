#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/step.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace StepsNs { // NOLINT(readability-identifier-naming): namespace partage du projet

struct FusionInput {
    int blockSize = DataNs::BLOCK_SIZE;
    std::string outputPath = "fusion_output";
    bool enableBlending = false;
};

struct FusionOutput {
    std::string mosaicPath = ""; // Dossier contenant les blocs couleur, provenance et masque.
    int totalBlocks = 0;
};

class FusionStepClass : public StepNs::StepClass {
public:
    explicit FusionStepClass(const FusionInput& initialInput = {});
    ~FusionStepClass() override = default;

    bool execute() override;
    const FusionOutput& getOutput() const { return output; }

private:
    FusionInput input;
    FusionOutput output;

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
                     cv::Mat& outBlockRgb, // NOLINT(bugprone-easily-swappable-parameters)
                     cv::Mat& outBlockProvenance,
                     cv::Mat& outBlockMask) const;
};

} // namespace StepsNs
