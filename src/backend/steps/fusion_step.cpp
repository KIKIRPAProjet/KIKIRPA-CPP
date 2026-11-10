// src/backend/steps/fusion_step.cpp
#include "steps/fusion_step.h"

#include "algorithms/brightness_equalization/output.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/file_fingerprint.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace fs = std::filesystem;

    namespace {

        constexpr int VALID_PIXEL = 255;   // valeur du masque pour un pixel fourni par une tuile
        constexpr int MAXIMUM_PROVENANCE_ID = std::numeric_limits<std::uint16_t>::max();
        constexpr int CHANNEL_COUNT_GRAY = 1;
        constexpr int CHANNEL_COUNT_COLOR = 3;
        constexpr int CHANNEL_COUNT_COLOR_ALPHA = 4;

        // Les trois fichiers de la mosaïque, écrits en parallèle bloc par bloc.
        struct MosaicWriters {
            IONs::TiledTiffWriterClass image;
            IONs::TiledTiffWriterClass provenance;
            IONs::TiledTiffWriterClass mask;
        };

        // Entrées du step, lues dans le DataStore.
        struct FusionInputs {
            const DataNs::TileList& tiles;
            const std::vector<DataNs::TilePositionData>& positions;
            const std::vector<DataNs::TileTerritory>& territories;
            const DataNs::IntensityCorrection* intensityCorrection = nullptr;   // nul : pas de correction
        };

        fs::path configuredOutputDirectory(const Json& configJson) {
            std::string directory;
            for (const Json& parameter : configJson.at("parameters"))
                if (parameter.at("name") == "outputDirectory") directory = parameter.at("value").get<std::string>();
            return directory.empty() ? fs::temp_directory_path() / "kikirpa-fusion" : fs::u8path(directory);
        }

        DataNs::Mosaic makeMosaicPaths(const fs::path& directory, cv::Size canvasSize) {
            return {directory / "mosaic_image.tif", directory / "mosaic_provenance.tif", directory / "mosaic_mask.tif",
                    canvasSize};
        }

        // Position de chaque tuile, retrouvée par le nom "rLL_cCC" lu dans son nom de fichier, comme graphcut.
        std::vector<cv::Point2d> findTilePositions(const FusionInputs& inputs) {
            std::map<std::string, cv::Point2d> positionByTileId;
            for (const DataNs::TilePositionData& tilePosition : inputs.positions)
                positionByTileId[tilePosition.tileId] = tilePosition.position;
            std::vector<cv::Point2d> positions;
            for (const auto& tile : inputs.tiles) {
                const std::string sourcePath = tile->getSource().string();
                int row = 0;
                int column = 0;
                std::string tileId;
                if (!GraphCutNs::ImagePretreatmentClass::parseTileName(sourcePath, row, column, tileId))
                    throw std::runtime_error("fusion: tile file name does not end with _rLL_cCC.tif: " + sourcePath);
                const auto positionEntry = positionByTileId.find(tileId);
                if (positionEntry == positionByTileId.end())
                    throw std::runtime_error("fusion: no position for tile '" + tileId + "'");
                positions.push_back(positionEntry->second);
            }
            return positions;
        }

        // Polygones du territoire de chaque tuile, par indice dans `tiles` ; une tuile sans territoire n'a aucun pixel.
        std::vector<std::vector<TerritoryPolygon>> indexTerritories(const FusionInputs& inputs) {
            std::vector<std::vector<TerritoryPolygon>> polygonsByTileIndex(inputs.tiles.size());
            for (const DataNs::TileTerritory& territory : inputs.territories) {
                if (territory.tileIndex < 0 || static_cast<std::size_t>(territory.tileIndex) >= inputs.tiles.size() ||
                    territory.holes.size() != territory.polygons.size())
                    throw std::runtime_error("fusion: invalid territory for tile index " +
                                             std::to_string(territory.tileIndex));
                for (std::size_t polygonIndex = 0; polygonIndex < territory.polygons.size(); ++polygonIndex) {
                    const std::vector<cv::Point>& outline = territory.polygons[polygonIndex];
                    if (outline.empty()) continue;
                    polygonsByTileIndex[static_cast<std::size_t>(territory.tileIndex)].push_back(
                        {outline, territory.holes[polygonIndex], cv::boundingRect(outline)});
                }
            }
            return polygonsByTileIndex;
        }

        // Gain et champ d'éclairage de la tuile `tileIndex` ; une tuile isolée n'est pas corrigée.
        void applyIntensityCorrection(const DataNs::IntensityCorrection& correction, std::size_t tileIndex,
                                      PlacedTile& placedTile) {
            const auto& isolated = correction.isolatedTileIndices;
            if (std::find(isolated.begin(), isolated.end(), static_cast<int>(tileIndex)) != isolated.end()) return;
            placedTile.gain = correction.tileGains[tileIndex];
            placedTile.fieldStrength = correction.fieldStrength;
            if (!(placedTile.gain > 0.0) || !std::isfinite(placedTile.gain))
                throw std::runtime_error("fusion: invalid intensity gain for tile index " + std::to_string(tileIndex));
        }

        void validateInputs(const FusionInputs& inputs) {
            if (inputs.tiles.empty()) throw std::runtime_error("fusion: no tile");
            if (inputs.positions.size() != inputs.tiles.size())
                throw std::runtime_error("fusion: the number of positions differs from the number of tiles");
            if (inputs.intensityCorrection && inputs.intensityCorrection->tileGains.size() != inputs.tiles.size())
                throw std::runtime_error("fusion: the number of intensity gains differs from the number of tiles");
        }

        PlacedTile placeTile(const DataNs::TileClass& tile, cv::Point corner) {
            if (tile.getTiff().empty())
                throw std::runtime_error("fusion: tile " + std::to_string(tile.getId()) + " has no pyramidal TIFF");
            if (tile.getId() < 0 || tile.getId() > MAXIMUM_PROVENANCE_ID)
                throw std::runtime_error("fusion: tile id " + std::to_string(tile.getId()) +
                                         " does not fit in the 16-bit provenance map");
            const DataNs::Level& fullResolution = tile.getLevel(0);
            PlacedTile placedTile;
            placedTile.tileId = tile.getId();
            placedTile.tiffPath = tile.getTiff();
            placedTile.mosaicRect = cv::Rect(corner, cv::Size(fullResolution.width, fullResolution.height));
            return placedTile;
        }

        FusionContext buildFusionContext(const FusionInputs& inputs) {
            validateInputs(inputs);
            const std::vector<cv::Point> corners = computeMosaicCorners(findTilePositions(inputs));
            std::vector<std::vector<TerritoryPolygon>> polygonsByTileIndex = indexTerritories(inputs);
            FusionContext context;
            for (std::size_t tileIndex = 0; tileIndex < inputs.tiles.size(); ++tileIndex) {
                PlacedTile placedTile = placeTile(*inputs.tiles[tileIndex], corners[tileIndex]);
                if (inputs.intensityCorrection) applyIntensityCorrection(*inputs.intensityCorrection, tileIndex, placedTile);
                placedTile.territory = std::move(polygonsByTileIndex[tileIndex]);
                context.canvasSize.width = std::max(context.canvasSize.width, placedTile.mosaicRect.br().x);
                context.canvasSize.height = std::max(context.canvasSize.height, placedTile.mosaicRect.br().y);
                context.tiles.push_back(std::move(placedTile));
            }
            return context;
        }

        // Même règle que GraphCutSeamFinderClass::verifyTiling : le trou est vidé, mais son contour appartient au
        // territoire (findContours place le contour d'un trou sur les pixels du territoire qui le bordent).
        void eraseHoles(cv::Mat& polygonMask, const std::vector<std::vector<cv::Point>>& holes, cv::Point offset) {
            if (holes.empty()) return;
            cv::drawContours(polygonMask, holes, -1, cv::Scalar(0), cv::FILLED, cv::LINE_8, cv::noArray(),
                             std::numeric_limits<int>::max(), offset);
            cv::drawContours(polygonMask, holes, -1, cv::Scalar(VALID_PIXEL), 1, cv::LINE_8, cv::noArray(),
                             std::numeric_limits<int>::max(), offset);
        }

        // Masque (VALID_PIXEL = pixel du territoire) de la zone `area` de la mosaïque. Chaque polygone est dessiné à
        // part puis ajouté : les trous d'un polygone n'effacent pas un autre polygone posé dedans.
        cv::Mat rasterizeTerritory(const std::vector<TerritoryPolygon>& territory, const cv::Rect& area) {
            cv::Mat territoryMask = cv::Mat::zeros(area.size(), CV_8UC1);
            cv::Mat polygonMask(area.size(), CV_8UC1);
            const cv::Point offset = -area.tl();
            for (const TerritoryPolygon& polygon : territory) {
                if ((polygon.boundingBox & area).empty()) continue;
                polygonMask.setTo(cv::Scalar(0));
                cv::drawContours(polygonMask, std::vector<std::vector<cv::Point>>{polygon.outline}, -1,
                                 cv::Scalar(VALID_PIXEL), cv::FILLED, cv::LINE_8, cv::noArray(),
                                 std::numeric_limits<int>::max(), offset);
                eraseHoles(polygonMask, polygon.holes, offset);
                territoryMask |= polygonMask;
            }
            return territoryMask;
        }

        // OpenCV range les canaux en BGR (et le TIFF pyramidal les a gardés ainsi) ; la mosaïque est en RGB.
        cv::Mat convertToRgb(const cv::Mat& image) {
            cv::Mat rgb;
            switch (image.channels()) {
                case CHANNEL_COUNT_GRAY: cv::cvtColor(image, rgb, cv::COLOR_GRAY2RGB); break;
                case CHANNEL_COUNT_COLOR: cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB); break;
                case CHANNEL_COUNT_COLOR_ALPHA: cv::cvtColor(image, rgb, cv::COLOR_BGRA2RGB); break;
                default: throw std::runtime_error("fusion: unsupported tile channel count");
            }
            return rgb;
        }

        // Pixels de la tuile qui tombent dans `overlap` (repère de la mosaïque), corrigés et en RGB 16 bits.
        // Les tuiles 8 bits passent en 16 bits (× 257) dans la correction.
        cv::Mat readCorrectedRgb(const PlacedTile& tile, const cv::Rect& overlap) {
            const cv::Rect regionInTile(overlap.tl() - tile.mosaicRect.tl(), overlap.size());
            cv::Mat region;
            std::string errorMessage;
            if (!IONs::PyramidalTiffReaderClass(tile.tiffPath).readRegion(0, regionInTile, region, errorMessage))
                throw std::runtime_error("fusion: cannot read tile " + std::to_string(tile.tileId) + ": " + errorMessage);
            if (region.depth() != CV_8U && region.depth() != CV_16U)
                throw std::runtime_error("fusion: tile " + std::to_string(tile.tileId) + " is not an 8 or 16-bit image");
            const BrightnessEqualizationNs::RegionInTile placement{regionInTile.tl(), tile.mosaicRect.size()};
            return convertToRgb(BrightnessEqualizationNs::OutputClass::correctRegion(region, placement, tile.gain,
                                                                                     tile.fieldStrength));
        }

        void renderTileInBlock(const PlacedTile& tile, DataNs::CompositeBlock& block) {
            const cv::Rect overlap = block.blockRect & tile.mosaicRect;
            if (overlap.empty()) return;
            const cv::Mat territoryMask = rasterizeTerritory(tile.territory, overlap);
            if (cv::countNonZero(territoryMask) == 0) return;   // aucun pixel de cette tuile ici : rien à lire
            const cv::Rect target(overlap.tl() - block.blockRect.tl(), overlap.size());
            readCorrectedRgb(tile, overlap).copyTo(block.imageRgb(target), territoryMask);
            block.provenance(target).setTo(cv::Scalar(tile.tileId), territoryMask);
            block.mask(target).setTo(cv::Scalar(VALID_PIXEL), territoryMask);
        }

        // Grille des blocs, ligne par ligne, depuis (0, 0) ; les blocs du bord droit et du bas sont plus petits.
        std::vector<cv::Rect> blockRectangles(cv::Size canvasSize) {
            std::vector<cv::Rect> rectangles;
            for (int blockY = 0; blockY < canvasSize.height; blockY += DataNs::BLOCK_SIZE)
                for (int blockX = 0; blockX < canvasSize.width; blockX += DataNs::BLOCK_SIZE)
                    rectangles.emplace_back(blockX, blockY, std::min(DataNs::BLOCK_SIZE, canvasSize.width - blockX),
                                            std::min(DataNs::BLOCK_SIZE, canvasSize.height - blockY));
            return rectangles;
        }

        bool openWriters(MosaicWriters& writers, const DataNs::Mosaic& mosaic, std::string& errorMessage) {
            return writers.image.open(mosaic.imagePath, mosaic.canvasSize, CV_16UC3, errorMessage) &&
                   writers.provenance.open(mosaic.provenancePath, mosaic.canvasSize, CV_16UC1, errorMessage) &&
                   writers.mask.open(mosaic.maskPath, mosaic.canvasSize, CV_8UC1, errorMessage);
        }

        bool writeBlock(MosaicWriters& writers, const DataNs::CompositeBlock& block, std::string& errorMessage) {
            const cv::Point origin = block.blockRect.tl();
            return writers.image.writeBlock(origin, block.imageRgb, errorMessage) &&
                   writers.provenance.writeBlock(origin, block.provenance, errorMessage) &&
                   writers.mask.writeBlock(origin, block.mask, errorMessage);
        }

        bool closeWriters(MosaicWriters& writers, std::string& errorMessage) {
            return writers.image.close(errorMessage) && writers.provenance.close(errorMessage) &&
                   writers.mask.close(errorMessage);
        }

        // Un bloc à la fois : calculé, écrit dans les trois fichiers, puis libéré. Après un échec, les writers
        // détruits suppriment leurs temporaires.
        void writeMosaic(const FusionContext& context, const DataNs::Mosaic& mosaic) {
            MosaicWriters writers;
            std::string errorMessage;
            bool isWritten = openWriters(writers, mosaic, errorMessage);
            for (const cv::Rect& blockRect : blockRectangles(context.canvasSize)) {
                if (!isWritten) break;
                isWritten = writeBlock(writers, renderBlock(blockRect, context), errorMessage);
            }
            if (!isWritten || !closeWriters(writers, errorMessage))
                throw std::runtime_error("fusion: cannot write the mosaic: " + errorMessage);
        }

        FusionInputs readInputs(const StepNs::DataStoreClass& dataStore) {
            const bool hasIntensityCorrection = dataStore.has("intensityCorrection");
            return {dataStore.get<DataNs::TileList>("tiles"),
                    dataStore.get<std::vector<DataNs::TilePositionData>>("positions"),
                    dataStore.get<std::vector<DataNs::TileTerritory>>("territories"),
                    hasIntensityCorrection ? &dataStore.get<DataNs::IntensityCorrection>("intensityCorrection") : nullptr};
        }

    }

    std::vector<cv::Point> computeMosaicCorners(const std::vector<cv::Point2d>& positions) {
        double minimumX = std::numeric_limits<double>::infinity();
        double minimumY = std::numeric_limits<double>::infinity();
        for (const cv::Point2d& position : positions) {
            minimumX = std::min(minimumX, position.x);
            minimumY = std::min(minimumY, position.y);
        }
        // Soustraire puis arrondir, dans cet ordre, comme graphcut : arrondir avant de soustraire peut décaler d'un
        // pixel (cvRound arrondit les demi-pixels au pair) et les territoires ne tomberaient plus sur les tuiles.
        const double offsetX = std::floor(minimumX);
        const double offsetY = std::floor(minimumY);
        std::vector<cv::Point> corners;
        corners.reserve(positions.size());
        for (const cv::Point2d& position : positions)
            corners.emplace_back(cvRound(position.x - offsetX), cvRound(position.y - offsetY));
        return corners;
    }

    DataNs::CompositeBlock renderBlock(const cv::Rect& blockRect, const FusionContext& context) {
        DataNs::CompositeBlock block{cv::Mat::zeros(blockRect.size(), CV_16UC3),
                                     cv::Mat::zeros(blockRect.size(), CV_16UC1),
                                     cv::Mat::zeros(blockRect.size(), CV_8UC1), blockRect};
        for (const PlacedTile& tile : context.tiles) renderTileInBlock(tile, block);
        return block;
    }

    FusionStepClass::FusionStepClass(const FusionInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "fusion";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"Positions", "positions"}, {"Territories", "territories"}};
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    Json FusionStepClass::externalInputsFingerprint() const {
        const DataNs::Mosaic mosaic = makeMosaicPaths(configuredOutputDirectory(configJson), {});
        return Json::array({IONs::computeFileFingerprint(mosaic.imagePath),
                            IONs::computeFileFingerprint(mosaic.provenancePath),
                            IONs::computeFileFingerprint(mosaic.maskPath)});
    }

    bool FusionStepClass::execute() {
        output = {};
        result = Json::object();
        try {
            if (!dataStore) throw std::runtime_error("fusion: data store is not available");
            // Après un échec, l'ancienne mosaïque ne doit plus être publiée.
            dataStore->replace(output.key, DataNs::Mosaic{});
            const FusionContext context = buildFusionContext(readInputs(*dataStore));
            output.value = makeMosaicPaths(configuredOutputDirectory(configJson), context.canvasSize);
            writeMosaic(context, output.value);
            dataStore->replace(output.key, output.value);
            result["image"] = output.value.imagePath.u8string();
            result["canvasSize"] = {output.value.canvasSize.width, output.value.canvasSize.height};
            result["blockCount"] = blockRectangles(output.value.canvasSize).size();
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

}
