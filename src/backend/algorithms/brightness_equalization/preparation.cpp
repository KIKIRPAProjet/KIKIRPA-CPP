// src/backend/algorithms/brightness_equalization/preparation.cpp
#include "algorithms/brightness_equalization/preparation.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace BrightnessEqualizationNs {

    void PreparationClass::validateParameters(const Parameters& parameters) {
        // Vérifié une fois à la construction : une valeur hors domaine est une erreur de configuration,
        // pas un échec d'estimation, d'où l'exception plutôt qu'un résultat vide
        if (parameters.reductionFactor < 1 || parameters.blurSigma < 0 || parameters.borderMargin < 0 ||
            parameters.lowThreshold < 0 || parameters.highThreshold > 1 || parameters.lowThreshold >= parameters.highThreshold ||
            parameters.iterationCount < 1 || parameters.huberThreshold <= 0 || parameters.gaugeWeight <= 0)
            throw std::invalid_argument("brightness equalization: invalid parameter (reductionFactor >= 1, "
                                        "iterationCount >= 1, blurSigma >= 0, borderMargin >= 0, "
                                        "0 <= lowThreshold < highThreshold <= 1, huberThreshold > 0, gaugeWeight > 0)");
    }

    ReducedTile PreparationClass::reduceTile(const cv::Mat& tile, const Parameters& parameters) {
        if (tile.empty()) return {};
        // Réduction AVANT la conversion en luminance : la tuile n'est jamais copiée en flottant pleine résolution
        // (1,2 Go pour une tuile couleur de 100 Mpx). La luminance et la moyenne INTER_AREA sont linéaires,
        // l'ordre ne change pas le résultat. Une tuile 8 bits passe d'abord en 16 bits (× 257) pour que la moyenne
        // ne soit pas arrondie au 1/255.
        cv::Mat source = tile;
        if (tile.depth() == CV_8U) tile.convertTo(source, CV_MAKETYPE(CV_16U, tile.channels()), 257.0);
        const cv::Size reducedSize(std::max(1, cvRound(tile.cols / static_cast<double>(parameters.reductionFactor))),
                                   std::max(1, cvRound(tile.rows / static_cast<double>(parameters.reductionFactor))));
        cv::Mat reducedImage;
        cv::resize(source, reducedImage, reducedSize, 0, 0, cv::INTER_AREA);
        return prepareReducedTile(reducedImage, tile.size(), parameters);
    }

    ReducedTile PreparationClass::prepareReducedTile(const cv::Mat& reducedImage, cv::Size fullSize,
                                                     const Parameters& parameters) {
        if (reducedImage.empty() || fullSize.area() <= 0) return {};
        ReducedTile reducedTile;
        reducedTile.luminance = computeLuminance(reducedImage);
        if (parameters.blurSigma > 0)
            cv::GaussianBlur(reducedTile.luminance, reducedTile.luminance, cv::Size(0, 0), parameters.blurSigma);
        reducedTile.fullSize = fullSize;
        return reducedTile;
    }

    std::string PreparationClass::validateInputs(const std::vector<ReducedTile>& tiles,
                                                 const std::vector<NeighbourPair>& pairs) {
        if (tiles.size() < 2) return "brightness equalization: at least 2 tiles are needed";
        if (pairs.empty()) return "brightness equalization: no pair of neighbouring tiles";
        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex)
            if (tiles[tileIndex].luminance.empty() || tiles[tileIndex].fullSize.area() <= 0)
                return "brightness equalization: tile " + std::to_string(tileIndex) + " is empty (unreadable?)";
        const int tileCount = static_cast<int>(tiles.size());
        for (std::size_t pairIndex = 0; pairIndex < pairs.size(); ++pairIndex) {
            const auto& pair = pairs[pairIndex];
            if (pair.tileIndexA < 0 || pair.tileIndexA >= tileCount || pair.tileIndexB < 0 || pair.tileIndexB >= tileCount ||
                pair.tileIndexA == pair.tileIndexB)
                return "brightness equalization: pair " + std::to_string(pairIndex) + " is invalid (" +
                       std::to_string(pair.tileIndexA) + ", " + std::to_string(pair.tileIndexB) + ") for " +
                       std::to_string(tileCount) + " tiles";
        }
        return {};
    }

    cv::Mat PreparationClass::computeLuminance(const cv::Mat& image) {
        cv::Mat luminance;
        image.convertTo(luminance, CV_MAKETYPE(CV_32F, image.channels()), 1.0 / maximumValueOfDepth(image.depth()));
        if (luminance.channels() == 3) cv::cvtColor(luminance, luminance, cv::COLOR_BGR2GRAY);         // cv::imread : ordre BGR
        else if (luminance.channels() == 4) cv::cvtColor(luminance, luminance, cv::COLOR_BGRA2GRAY);   // alpha ignoré
        else if (luminance.channels() != 1) cv::extractChannel(luminance, luminance, 0);
        return luminance;
    }

    double PreparationClass::maximumValueOfDepth(int depth) {
        switch (depth) {
            case CV_8U:  return 255.0;
            case CV_8S:  return 127.0;
            case CV_16U: return 65535.0;
            case CV_16S: return 32767.0;
            case CV_32S: return 2147483647.0;
            default:     return 1.0;   // flottant : supposé déjà dans [0, 1]
        }
    }

}
