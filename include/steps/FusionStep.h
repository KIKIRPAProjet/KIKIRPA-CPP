#pragma once

#include "Backend/core/Step.h"
#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <memory>

namespace stitching::steps {

struct FusionInput {
    int blockSize = 512;          // Taille d'un bloc de composition (ex. 512x512)
    std::string outputPath = "";  // Dossier ou préfixe de sortie pour les blocs
    bool enableBlending = false;  // Transition étroite (optionnel)
};

class FusionStep : public Step {
public:
    explicit FusionStep(const FusionInput& input = FusionInput{});
    ~FusionStep() override = default;

    bool execute() override;

private:
    // Structure interne pour associer une tuile à sa boîte englobante absolue
    struct TilePlacement {
        int id;
        std::string path;
        cv::Rect globalBox;
        double lightGain = 1.0;
    };

    // Calcul de l'enveloppe totale de la mosaïque
    cv::Rect computeGlobalBoundingBox(const std::vector<TilePlacement>& placements);

    // Composition d'un bloc individuel
    void renderBlock(
        const cv::Rect& blockRect,
        const std::vector<TilePlacement>& placements,
        const cv::Mat& indexMatrix,
        cv::Mat& outBlockRgb,
        cv::Mat& outBlockProvenance,
        cv::Mat& outBlockMask
    );
};

} // namespace stitching::steps