// src/backend/steps/fusion_step.cpp
#include "steps/fusion_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

namespace {
constexpr int VALID_MASK_VALUE = 255;
constexpr double ROTATION_TOLERANCE = 1e-9;
constexpr double MIN_GAIN_THRESHOLD = 1e-6;
constexpr double SCALE_8_TO_16_BIT = 257.0;
constexpr double INITIAL_MIN_COORDINATE = 1e300;
constexpr std::size_t MIN_POLYGON_VERTICES = 3;
} // namespace

FusionStepClass::FusionStepClass(const FusionInput& initialInput)
    : input(initialInput) {
    ConfigNs::StepConfig stepConfig;
    stepConfig.name = "fusion";
    stepConfig.parameters = {
        {"int", "blockSize", initialInput.blockSize},
        {"string", "outputPath", initialInput.outputPath},
        {"bool", "enableBlending", initialInput.enableBlending}
    };
    stepConfig.inputs = {
        {"Tiles", "tiles"},
        {"Positions", "positions"},
        {"Territories", "territories"}
    };
    stepConfig.outputs = {
        {"CompositeBlocks", "compositeBlocks"}
    };
    configJson = stepConfig;
}

cv::Rect FusionStepClass::computeGlobalBoundingBox(
    const std::vector<TilePlacement>& placements) {
    if (placements.empty()) return {};
    cv::Rect result = placements.front().globalBox;
    for (std::size_t i = 1; i < placements.size(); ++i) {
        result |= placements[i].globalBox;
    }
    return result;
}

cv::Mat FusionStepClass::makeTerritoryMask(
    const cv::Rect& block, const DataNs::TileTerritory& territory) {
    cv::Mat mask(block.size(), CV_8UC1, cv::Scalar(0));
    for (std::size_t p = 0; p < territory.polygons.size(); ++p) {
        const auto& outer = territory.polygons[p];
        if (outer.size() < MIN_POLYGON_VERTICES) continue;

        std::vector<cv::Point> shifted;
        shifted.reserve(outer.size());
        for (const auto& point : outer) {
            shifted.emplace_back(point.x - block.x, point.y - block.y);
        }
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, cv::Scalar(VALID_MASK_VALUE));

        if (p < territory.holes.size()) {
            for (const auto& hole : territory.holes[p]) {
                if (hole.size() < MIN_POLYGON_VERTICES) continue;
                std::vector<cv::Point> shiftedHole;
                shiftedHole.reserve(hole.size());
                for (const auto& point : hole) {
                    shiftedHole.emplace_back(point.x - block.x, point.y - block.y);
                }
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shiftedHole}, cv::Scalar(0));
            }
        }
    }
    return mask;
}

void FusionStepClass::renderBlock(
    const cv::Rect& block,
    const std::vector<TilePlacement>& placements,
    const std::vector<DataNs::TileTerritory>& territories,
    cv::Mat& outBlockRgb,
    cv::Mat& outBlockProvenance,
    cv::Mat& outBlockMask) const {

    outBlockRgb = cv::Mat::zeros(block.size(), CV_16UC3);
    outBlockProvenance = cv::Mat::zeros(block.size(), CV_16UC1);
    outBlockMask = cv::Mat::zeros(block.size(), CV_8UC1);

    std::map<int, const DataNs::TileTerritory*> territoryByIndex;
    for (const auto& territory : territories) {
        territoryByIndex[territory.tileIndex] = &territory;
    }

    for (const auto& tile : placements) {
        auto territoryIt = territoryByIndex.find(tile.index);
        if (territoryIt == territoryByIndex.end()) continue;

        const cv::Rect intersection = block & tile.globalBox;
        if (intersection.empty()) continue;

        cv::Mat source = cv::imread(tile.path.string(), cv::IMREAD_UNCHANGED);
        if (source.empty()) {
            throw std::runtime_error("cannot read tile: " + tile.path.string());
        }

        if (source.type() == CV_8UC3) {
            source.convertTo(source, CV_16UC3, SCALE_8_TO_16_BIT);
        } else if (source.type() == CV_8UC1) {
            cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
            source.convertTo(source, CV_16UC3, SCALE_8_TO_16_BIT);
        } else if (source.type() == CV_16UC1) {
            cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
        }

        if (source.type() != CV_16UC3) {
            throw std::runtime_error("tile cannot be converted to CV_16UC3: " + tile.path.string());
        }
        if (source.size() != tile.globalBox.size()) {
            throw std::runtime_error("unexpected tile dimensions: " + tile.path.string());
        }

        const cv::Rect srcRect(intersection.x - tile.globalBox.x,
                               intersection.y - tile.globalBox.y,
                               intersection.width, intersection.height);
        const cv::Rect dstRect(intersection.x - block.x,
                               intersection.y - block.y,
                               intersection.width, intersection.height);

        cv::Mat territoryMask = makeTerritoryMask(block, *territoryIt->second);
        cv::Mat maskRoi = territoryMask(dstRect);

        cv::Mat pixels = source(srcRect);
        cv::Mat corrected;
        pixels.convertTo(corrected, CV_16UC3, tile.lightGain);

        corrected.copyTo(outBlockRgb(dstRect), maskRoi);
        outBlockProvenance(dstRect).setTo(cv::Scalar(tile.index + 1), maskRoi);
        outBlockMask(dstRect).setTo(cv::Scalar(VALID_MASK_VALUE), maskRoi);
    }
}

bool FusionStepClass::execute() {
    output = {};
    result = Json::object();
    try {
        if (!dataStore) {
            throw std::runtime_error("data store is not available");
        }
        if (!dataStore->has("tiles") || !dataStore->has("positions") ||
            !dataStore->has("territories")) {
            throw std::runtime_error("missing required input 'tiles', 'positions', or 'territories'");
        }

        int blockSize = input.blockSize;
        std::string outputPath = input.outputPath;
        bool enableBlending = input.enableBlending;
        if (hasParameter("blockSize")) blockSize = getParameter("blockSize").get<int>();
        if (hasParameter("outputPath")) outputPath = getParameter("outputPath").get<std::string>();
        if (hasParameter("enableBlending")) enableBlending = getParameter("enableBlending").get<bool>();

        if (blockSize <= 0) {
            throw std::runtime_error("blockSize must be positive");
        }
        if (enableBlending) {
            throw std::runtime_error("blending is not implemented yet");
        }

        const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
        const auto& positions =
            dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
        const auto& territories =
            dataStore->get<std::vector<DataNs::TileTerritory>>("territories");

        if (tiles.empty() || positions.size() != tiles.size()) {
            throw std::runtime_error("mismatched tile and position counts");
        }
        if (territories.empty()) {
            throw std::runtime_error("no territories provided");
        }

        double minX = INITIAL_MIN_COORDINATE;
        double minY = INITIAL_MIN_COORDINATE;
        for (const auto& pos : positions) {
            minX = std::min(minX, pos.position.x);
            minY = std::min(minY, pos.position.y);
        }
        const double offsetX = std::floor(minX);
        const double offsetY = std::floor(minY);

        std::vector<double> tileGains(tiles.size(), 1.0);
        if (dataStore->has("intensityCorrection")) {
            const auto& ic = dataStore->get<DataNs::IntensityCorrection>("intensityCorrection");
            for (std::size_t i = 0; i < tiles.size() && i < ic.tileGains.size(); ++i) {
                if (ic.tileGains[i] > MIN_GAIN_THRESHOLD) {
                    tileGains[i] = 1.0 / ic.tileGains[i];
                }
            }
        }

        std::vector<TilePlacement> placements;
        placements.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto& tile = tiles[i];
            if (!tile) throw std::runtime_error("null tile in tiles list");

            const std::string filename = tile->getSource().filename().string();
            const DataNs::TilePositionData* position = nullptr;

            for (const auto& candidate : positions) {
                if (filename.find(candidate.tileId) != std::string::npos ||
                    candidate.tileId == std::to_string(tile->getId())) {
                    position = &candidate;
                    break;
                }
            }
            if (!position && i < positions.size()) {
                position = &positions[i];
            }
            if (!position) {
                throw std::runtime_error("cannot find position for " + filename);
            }
            if (std::abs(position->rotationRadians) > ROTATION_TOLERANCE) {
                throw std::runtime_error("rotations are not supported yet");
            }

            const auto& level = tile->getLevel(0);
            if (level.width <= 0 || level.height <= 0) {
                throw std::runtime_error("invalid tile dimensions");
            }

            placements.push_back(TilePlacement{
                static_cast<int>(i),
                tile->getSource(),
                cv::Rect(cvRound(position->position.x - offsetX),
                         cvRound(position->position.y - offsetY),
                         level.width, level.height),
                tileGains[i]
            });
        }

        for (const auto& territory : territories) {
            if (territory.tileIndex < 0 ||
                territory.tileIndex >= static_cast<int>(tiles.size())) {
                throw std::runtime_error("territory tileIndex out of bounds");
            }
        }

        const cv::Rect bounds = computeGlobalBoundingBox(placements);
        if (bounds.empty()) {
            throw std::runtime_error("empty mosaic bounds");
        }

        std::filesystem::path outputDir;
        if (!outputPath.empty()) {
            outputDir = outputPath;
            std::filesystem::create_directories(outputDir);
        }

        DataNs::CompositeBlockList compositeBlocks;

        for (int y = bounds.y; y < bounds.y + bounds.height; y += blockSize) {
            for (int x = bounds.x; x < bounds.x + bounds.width; x += blockSize) {
                const cv::Rect block(x, y,
                    std::min(blockSize, bounds.x + bounds.width - x),
                    std::min(blockSize, bounds.y + bounds.height - y));

                cv::Mat rgb, provenance, mask;
                renderBlock(block, placements, territories, rgb, provenance, mask);

                if (!outputDir.empty()) {
                    const std::string suffix = "_x" + std::to_string(x) +
                                               "_y" + std::to_string(y) + ".png";
                    cv::imwrite((outputDir / ("rgb" + suffix)).string(), rgb);
                    cv::imwrite((outputDir / ("provenance" + suffix)).string(), provenance);
                    cv::imwrite((outputDir / ("mask" + suffix)).string(), mask);
                }

                DataNs::CompositeBlock compBlock;
                compBlock.imageRgb = rgb;
                compBlock.provenance = provenance;
                compBlock.mask = mask;
                compBlock.blockRect = block;
                compositeBlocks.push_back(std::move(compBlock));
                ++output.totalBlocks;
            }
        }

        dataStore->replace("compositeBlocks", compositeBlocks);

        output.mosaicPath = outputPath;
        result["mosaicPath"] = output.mosaicPath;
        result["totalBlocks"] = output.totalBlocks;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "FusionStep error: " << e.what() << '\n';
        result["error"] = e.what();
        return false;
    }
}

} // namespace StepsNs
