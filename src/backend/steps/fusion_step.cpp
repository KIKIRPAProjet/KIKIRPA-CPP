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
#include <filesystem>
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

        struct TilePlacement {
            int index = 0;
            std::filesystem::path path;
            cv::Rect globalBox;
            double lightGain = 1.0;
        };

        struct BlockRenderOutput {
            cv::Mat rgb;
            cv::Mat provenance;
            cv::Mat mask;
        };

        struct GridContext {
            cv::Rect bounds;
            int blockSize = 0;
            std::filesystem::path outputDir;
        };

        cv::Rect computeGlobalBoundingBox(const std::vector<TilePlacement>& placements) {
            if (placements.empty()) return {};
            cv::Rect result = placements.front().globalBox;
            for (std::size_t i = 1; i < placements.size(); ++i) {
                result |= placements[i].globalBox;
            }
            return result;
        }

        cv::Mat makeTerritoryMask(const cv::Rect& block, const DataNs::TileTerritory& territory) {
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

        cv::Mat loadAndNormalizeTileImage(const TilePlacement& tile) {
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
            return source;
        }

        void renderTileOntoBlock(const cv::Rect& block, const TilePlacement& tile,
                                 const DataNs::TileTerritory& territory, BlockRenderOutput& out) {
            const cv::Rect intersection = block & tile.globalBox;
            if (intersection.empty()) return;

            const cv::Mat source = loadAndNormalizeTileImage(tile);
            const cv::Rect srcRect(intersection.x - tile.globalBox.x,
                                   intersection.y - tile.globalBox.y,
                                   intersection.width, intersection.height);
            const cv::Rect dstRect(intersection.x - block.x,
                                   intersection.y - block.y,
                                   intersection.width, intersection.height);

            const cv::Mat territoryMask = makeTerritoryMask(block, territory);
            const cv::Mat maskRoi = territoryMask(dstRect);

            cv::Mat pixels = source(srcRect);
            cv::Mat corrected;
            pixels.convertTo(corrected, CV_16UC3, tile.lightGain);

            corrected.copyTo(out.rgb(dstRect), maskRoi);
            out.provenance(dstRect).setTo(cv::Scalar(tile.index + 1), maskRoi);
            out.mask(dstRect).setTo(cv::Scalar(VALID_MASK_VALUE), maskRoi);
        }

        void renderBlock(const cv::Rect& block, const std::vector<TilePlacement>& placements,
                         const std::map<int, const DataNs::TileTerritory*>& territoryByIndex,
                         BlockRenderOutput& out) {
            out.rgb = cv::Mat::zeros(block.size(), CV_16UC3);
            out.provenance = cv::Mat::zeros(block.size(), CV_16UC1);
            out.mask = cv::Mat::zeros(block.size(), CV_8UC1);

            for (const auto& tile : placements) {
                const auto territoryIt = territoryByIndex.find(tile.index);
                if (territoryIt != territoryByIndex.end()) {
                    renderTileOntoBlock(block, tile, *territoryIt->second, out);
                }
            }
        }

        std::vector<double> computeTileGains(const StepNs::DataStoreClass& store, std::size_t tileCount) {
            std::vector<double> tileGains(tileCount, 1.0);
            if (store.has("intensityCorrection")) {
                const auto& ic = store.get<DataNs::IntensityCorrection>("intensityCorrection");
                for (std::size_t i = 0; i < tileCount && i < ic.tileGains.size(); ++i) {
                    if (ic.tileGains[i] > MIN_GAIN_THRESHOLD) {
                        tileGains[i] = 1.0 / ic.tileGains[i];
                    }
                }
            }
            return tileGains;
        }

        const DataNs::TilePositionData* findTilePosition(const std::string& filename, int tileId,
                                                         std::size_t index,
                                                         const std::vector<DataNs::TilePositionData>& positions) {
            for (const auto& candidate : positions) {
                if (filename.find(candidate.tileId) != std::string::npos ||
                    candidate.tileId == std::to_string(tileId)) {
                    return &candidate;
                }
            }
            if (index < positions.size()) return &positions[index];
            return nullptr;
        }

        std::vector<TilePlacement> buildTilePlacements(const DataNs::TileList& tiles,
                                                       const std::vector<DataNs::TilePositionData>& positions,
                                                       const std::vector<double>& tileGains) {
            double minX = INITIAL_MIN_COORDINATE;
            double minY = INITIAL_MIN_COORDINATE;
            for (const auto& pos : positions) {
                minX = std::min(minX, pos.position.x);
                minY = std::min(minY, pos.position.y);
            }
            const double offsetX = std::floor(minX);
            const double offsetY = std::floor(minY);

            std::vector<TilePlacement> placements;
            placements.reserve(tiles.size());
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                const auto& tile = tiles[i];
                if (!tile) throw std::runtime_error("null tile in tiles list");

                const std::string filename = tile->getSource().filename().string();
                const auto* position = findTilePosition(filename, tile->getId(), i, positions);
                if (!position) throw std::runtime_error("cannot find position for " + filename);
                if (std::abs(position->rotationRadians) > ROTATION_TOLERANCE) {
                    throw std::runtime_error("rotations are not supported yet");
                }

                const auto& level = tile->getLevel(0);
                if (level.width <= 0 || level.height <= 0) throw std::runtime_error("invalid tile dimensions");

                const cv::Rect box(cvRound(position->position.x - offsetX),
                                   cvRound(position->position.y - offsetY),
                                   level.width, level.height);
                placements.push_back(TilePlacement{static_cast<int>(i), tile->getSource(), box, tileGains[i]});
            }
            return placements;
        }

        void saveIntermediateBlockImages(const std::filesystem::path& outputDir, int blockX, int blockY,
                                         const BlockRenderOutput& renderOutput) {
            if (outputDir.empty()) return;
            const std::string suffix = "_x" + std::to_string(blockX) + "_y" + std::to_string(blockY) + ".png";
            cv::imwrite((outputDir / ("rgb" + suffix)).string(), renderOutput.rgb);
            cv::imwrite((outputDir / ("provenance" + suffix)).string(), renderOutput.provenance);
            cv::imwrite((outputDir / ("mask" + suffix)).string(), renderOutput.mask);
        }

        void renderMosaicGrid(const GridContext& grid,
                              const std::vector<TilePlacement>& placements,
                              const std::vector<DataNs::TileTerritory>& territories,
                              DataNs::CompositeBlockList& blocks) {
            std::map<int, const DataNs::TileTerritory*> territoryByIndex;
            for (const auto& territory : territories) {
                territoryByIndex[territory.tileIndex] = &territory;
            }

            for (int y = grid.bounds.y; y < grid.bounds.y + grid.bounds.height; y += grid.blockSize) {
                for (int x = grid.bounds.x; x < grid.bounds.x + grid.bounds.width; x += grid.blockSize) {
                    const cv::Rect block(x, y,
                        std::min(grid.blockSize, grid.bounds.x + grid.bounds.width - x),
                        std::min(grid.blockSize, grid.bounds.y + grid.bounds.height - y));

                    BlockRenderOutput renderOutput;
                    renderBlock(block, placements, territoryByIndex, renderOutput);
                    saveIntermediateBlockImages(grid.outputDir, x, y, renderOutput);

                    DataNs::CompositeBlock compBlock;
                    compBlock.imageRgb = std::move(renderOutput.rgb);
                    compBlock.provenance = std::move(renderOutput.provenance);
                    compBlock.mask = std::move(renderOutput.mask);
                    compBlock.blockRect = block;
                    blocks.push_back(std::move(compBlock));
                }
            }
        }

        void prepareAndRenderMosaic(const StepNs::DataStoreClass& store,
                                    const GridContext& grid,
                                    DataNs::CompositeBlockList& compositeBlocks) {
            const auto& tiles = store.get<DataNs::TileList>("tiles");
            const auto& positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
            const auto& territories = store.get<std::vector<DataNs::TileTerritory>>("territories");

            if (tiles.empty() || positions.size() != tiles.size()) {
                throw std::runtime_error("mismatched tile and position counts");
            }
            if (territories.empty()) throw std::runtime_error("no territories provided");

            const auto tileGains = computeTileGains(store, tiles.size());
            const auto placements = buildTilePlacements(tiles, positions, tileGains);
            const cv::Rect bounds = computeGlobalBoundingBox(placements);
            if (bounds.empty()) throw std::runtime_error("empty mosaic bounds");

            if (!grid.outputDir.empty()) {
                std::filesystem::create_directories(grid.outputDir);
            }
            const GridContext actualGrid{bounds, grid.blockSize, grid.outputDir};
            renderMosaicGrid(actualGrid, placements, territories, compositeBlocks);
        }

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

    bool FusionStepClass::execute() {
        output = {};
        result = Json::object();
        try {
            if (!dataStore) throw std::runtime_error("data store is not available");
            if (!dataStore->has("tiles") || !dataStore->has("positions") || !dataStore->has("territories")) {
                throw std::runtime_error("missing required input 'tiles', 'positions', or 'territories'");
            }

            int blockSize = input.blockSize;
            std::string outputPath = input.outputPath;
            bool enableBlending = input.enableBlending;
            try {
                blockSize = getParameter("blockSize").get<int>();
                outputPath = getParameter("outputPath").get<std::string>();
                enableBlending = getParameter("enableBlending").get<bool>();
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
            }

            if (blockSize <= 0) throw std::runtime_error("blockSize must be positive");
            if (enableBlending) throw std::runtime_error("blending is not implemented yet");

            DataNs::CompositeBlockList compositeBlocks;
            const GridContext requestedGrid{{}, blockSize, outputPath};
            prepareAndRenderMosaic(*dataStore, requestedGrid, compositeBlocks);

            output.totalBlocks = static_cast<int>(compositeBlocks.size());
            dataStore->replace("compositeBlocks", compositeBlocks);
            output.mosaicPath = outputPath;
            result["mosaicPath"] = output.mosaicPath;
            result["totalBlocks"] = output.totalBlocks;
            return true;
        } catch (const std::exception& e) {
            result["error"] = e.what();
            return false;
        }
    }

} // namespace StepsNs
