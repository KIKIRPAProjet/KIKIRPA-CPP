
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <algorithm>
#include "steps/FusionStep.h"
namespace StepsNs {

FusionStepClass::FusionStepClass(const FusionInput& initialInput)
    : input_(initialInput) {
}

cv::Rect FusionStepClass::computeGlobalBoundingBox(const std::vector<TilePlacement>& placements) {
    if (placements.empty()) {
        return cv::Rect();
    }
    cv::Rect bbox = placements.front().globalBox;
    for (size_t i = 1; i < placements.size(); ++i) {
        bbox |= placements[i].globalBox;
    }
    return bbox;
}

void FusionStepClass::renderBlock(
    const cv::Rect& blockRect,
    const std::vector<TilePlacement>& placements,
    const cv::Mat& indexMatrix,
    cv::Mat& outBlockRgb,
    cv::Mat& outBlockProvenance,
    cv::Mat& outBlockMask) 
{
    // Buffer RGB 16 bits, carte d'indices et masque de validité
    outBlockRgb = cv::Mat::zeros(blockRect.size(), CV_16UC3);
    outBlockProvenance = cv::Mat::zeros(blockRect.size(), CV_16UC1);
    outBlockMask = cv::Mat::zeros(blockRect.size(), CV_8UC1);

    for (const auto& tile : placements) {
        cv::Rect intersection = blockRect & tile.globalBox;
        if (intersection.empty()) continue;

        cv::Mat srcImg = cv::imread(tile.path, cv::IMREAD_UNCHANGED);
        if (srcImg.empty()) continue;

        cv::Rect tileRoi(
            intersection.x - tile.globalBox.x,
            intersection.y - tile.globalBox.y,
            intersection.width,
            intersection.height
        );
        cv::Rect blockRoi(
            intersection.x - blockRect.x,
            intersection.y - blockRect.y,
            intersection.width,
            intersection.height
        );

        cv::Mat subImg = srcImg(tileRoi);

        for (int y = 0; y < intersection.height; ++y) {
            for (int x = 0; x < intersection.width; ++x) {
                int globalX = intersection.x + x;
                int globalY = intersection.y + y;

                bool isOwner = true;
                if (!indexMatrix.empty() && globalY < indexMatrix.rows && globalX < indexMatrix.cols) {
                    isOwner = (indexMatrix.at<int>(globalY, globalX) == tile.id);
                }

                if (isOwner) {
                    if (subImg.type() == CV_16UC3) {
                        outBlockRgb.at<cv::Vec3w>(blockRoi.y + y, blockRoi.x + x) = 
                            subImg.at<cv::Vec3w>(y, x) * tile.lightGain;
                    } else if (subImg.type() == CV_8UC3) {
                        cv::Vec3b pix = subImg.at<cv::Vec3b>(y, x);
                        outBlockRgb.at<cv::Vec3w>(blockRoi.y + y, blockRoi.x + x) = 
                            cv::Vec3w(pix[0] * 257, pix[1] * 257, pix[2] * 257) * tile.lightGain;
                    }
                    outBlockProvenance.at<uint16_t>(blockRoi.y + y, blockRoi.x + x) = static_cast<uint16_t>(tile.id);
                    outBlockMask.at<uint8_t>(blockRoi.y + y, blockRoi.x + x) = 255;
                }
            }
        }
    }
}

bool FusionStepClass::execute() {
    std::cout << "[FusionStep] Exécution du découpage par blocs (taille=" 
              << input_.blockSize << ")..." << std::endl;

    std::vector<TilePlacement> placements;
    cv::Rect globalBBox = computeGlobalBoundingBox(placements);

    if (globalBBox.empty()) {
        std::cout << "[FusionStep] Aucune tuile à composer." << std::endl;
        return true;
    }

    cv::Mat seamMatrix;

    for (int y = globalBBox.y; y < globalBBox.y + globalBBox.height; y += input_.blockSize) {
        for (int x = globalBBox.x; x < globalBBox.x + globalBBox.width; x += input_.blockSize) {
            cv::Rect blockRect(
                x, y,
                std::min(input_.blockSize, globalBBox.x + globalBBox.width - x),
                std::min(input_.blockSize, globalBBox.y + globalBBox.height - y)
            );

            cv::Mat blockRgb, blockProv, blockMask;
            renderBlock(blockRect, placements, seamMatrix, blockRgb, blockProv, blockMask);
        }
    }

    std::cout << "[FusionStep] Rendu terminé avec succès." << std::endl;
    return true;
}

} // namespace StepsNs