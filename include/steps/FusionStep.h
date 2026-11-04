#pragma once

#include "core/data/tile.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;

    struct FusionInput {
        int blockSize = 512;          // Largeur et hauteur d'un bloc (ex: 512x512)
        std::string outputPath = "";  // Préfixe ou dossier de sortie
        bool enableBlending = false;  // Lissage optionnel
    };

    struct FusionOutput {
        std::string mosaicPath = "";
        int totalBlocks = 0;
    };

    class FusionStepClass : public StepNs::StepClass {
    public:
        explicit FusionStepClass(const FusionInput& initialInput = {});
        
        virtual ~FusionStepClass() = default;

        bool execute() override;
        const FusionOutput& getOutput() const { return output_; }

    private:
        FusionInput input_;
        FusionOutput output_;

        struct TilePlacement {
            int id = 0;
            std::string path;
            cv::Rect globalBox;
            double lightGain = 1.0;
        };

        cv::Rect computeGlobalBoundingBox(const std::vector<TilePlacement>& placements);

        void renderBlock(
            const cv::Rect& blockRect,
            const std::vector<TilePlacement>& placements,
            const cv::Mat& indexMatrix,
            cv::Mat& outBlockRgb,
            cv::Mat& outBlockProvenance,
            cv::Mat& outBlockMask
        );
    };

} // namespace StepsNs