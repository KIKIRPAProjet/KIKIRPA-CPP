#include "steps/FusionStep.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
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
#include <vector>

namespace StepsNs { // NOLINT(readability-identifier-naming): namespace partage du projet

namespace {
constexpr int VALID_MASK_VALUE = 255;
constexpr double ROTATION_TOLERANCE = 1e-9;
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
    stepConfig.outputs = {}; // Contrat DataStore de sortie a definir avec l'equipe.
    configJson = stepConfig;
}

cv::Rect FusionStepClass::computeGlobalBoundingBox(
    const std::vector<TilePlacement>& placements) {
    if (placements.empty()) return {};
    cv::Rect result = placements.front().globalBox;
    for (std::size_t i = 1; i < placements.size(); ++i)
        result |= placements[i].globalBox;
    return result;
}

// NOLINTNEXTLINE(readability-function-size): polygones et trous doivent rester synchronises
cv::Mat FusionStepClass::makeTerritoryMask(
    const cv::Rect& block, const DataNs::TileTerritory& territory) {
    cv::Mat mask(block.size(), CV_8UC1, cv::Scalar(0));
    for (std::size_t p = 0; p < territory.polygons.size(); ++p) {
        const auto& outer = territory.polygons[p];
        if (outer.size() < 3) continue;
        std::vector<cv::Point> shifted;
        shifted.reserve(outer.size());
        for (const auto& point : outer)
            shifted.emplace_back(point.x - block.x, point.y - block.y);
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, cv::Scalar(VALID_MASK_VALUE));
        if (p < territory.holes.size()) {
            for (const auto& hole : territory.holes[p]) {
                if (hole.size() < 3) continue;
                std::vector<cv::Point> shiftedHole;
                shiftedHole.reserve(hole.size());
                for (const auto& point : hole)
                    shiftedHole.emplace_back(point.x - block.x, point.y - block.y);
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shiftedHole}, cv::Scalar(0));
            }
        }
    }
    return mask;
}

// NOLINTNEXTLINE(readability-function-size): controle et rendu du bloc dans une operation
void FusionStepClass::renderBlock(
    const cv::Rect& block,
    const std::vector<TilePlacement>& placements,
    const std::vector<DataNs::TileTerritory>& territories,
    cv::Mat& outBlockRgb, // NOLINT(bugprone-easily-swappable-parameters)
    cv::Mat& outBlockProvenance,
    cv::Mat& outBlockMask) const {

    // OpenCV imread charge en BGR ; nom outBlockRgb conserve pour compatibilite.
    outBlockRgb = cv::Mat::zeros(block.size(), CV_16UC3);
    outBlockProvenance = cv::Mat::zeros(block.size(), CV_16UC1);
    outBlockMask = cv::Mat::zeros(block.size(), CV_8UC1);

    std::map<int, const DataNs::TileTerritory*> territoryByIndex;
    for (const auto& territory : territories)
        territoryByIndex[territory.tileIndex] = &territory;

    for (const auto& tile : placements) {
        auto territoryIt = territoryByIndex.find(tile.index);
        if (territoryIt == territoryByIndex.end()) continue;
        const cv::Rect INTERSECTION = block & tile.globalBox;
        if (INTERSECTION.empty()) continue;

        cv::Mat source = cv::imread(tile.path.string(), cv::IMREAD_UNCHANGED);
        if (source.empty())
            throw std::runtime_error("Impossible de lire la tuile : " + tile.path.string());
        if (source.type() != CV_16UC3)
            throw std::runtime_error("Tuile non CV_16UC3 : " + tile.path.string());
        if (source.size() != tile.globalBox.size())
            throw std::runtime_error("Dimensions inattendues : " + tile.path.string());

        const cv::Rect SRC_RECT(INTERSECTION.x - tile.globalBox.x,
                               INTERSECTION.y - tile.globalBox.y,
                               INTERSECTION.width, INTERSECTION.height);
        const cv::Rect DST_RECT(INTERSECTION.x - block.x,
                               INTERSECTION.y - block.y,
                               INTERSECTION.width, INTERSECTION.height);

        cv::Mat territoryMask = makeTerritoryMask(block, *territoryIt->second);
        cv::Mat maskRoi = territoryMask(DST_RECT);
        cv::Mat pixels = source(SRC_RECT);
        cv::Mat corrected;
        pixels.convertTo(corrected, CV_16UC3, tile.lightGain);
        corrected.copyTo(outBlockRgb(DST_RECT), maskRoi);
        outBlockProvenance(DST_RECT).setTo(cv::Scalar(tile.index + 1), maskRoi);
        outBlockMask(DST_RECT).setTo(cv::Scalar(VALID_MASK_VALUE), maskRoi);
    }
}

// NOLINTNEXTLINE(readability-function-size): orchestration de la lecture et ecriture des blocs
bool FusionStepClass::execute() {
    output = {};
    try {
        if (!dataStore)
            throw std::runtime_error("FusionStep : DataStore non initialise");
        if (!dataStore->has("tiles") || !dataStore->has("positions") ||
            !dataStore->has("territories"))
            throw std::runtime_error("FusionStep : tiles, positions ou territories manquant");

        const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
        const auto& positions =
            dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
        const auto& territories =
            dataStore->get<std::vector<DataNs::TileTerritory>>("territories");

        if (tiles.empty() || positions.size() != tiles.size())
            throw std::runtime_error("FusionStep : nombre de tuiles/positions invalide");
        if (territories.empty())
            throw std::runtime_error("FusionStep : aucun territoire");
        if (input.blockSize <= 0)
            throw std::runtime_error("FusionStep : blockSize doit etre positif");
        if (input.enableBlending)
            throw std::runtime_error("FusionStep : blending pas encore implemente");

        // L'identifiant de position rLL_cCC est present dans le nom de fichier.
        std::vector<TilePlacement> placements;
        placements.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto& tile = tiles[i];
            if (!tile) throw std::runtime_error("FusionStep : tuile nulle");
            const std::string FILENAME = tile->getSource().filename().string();
            const DataNs::TilePositionData* position = nullptr;
            for (const auto& candidate : positions) {
                if (FILENAME.find(candidate.tileId) != std::string::npos) {
                    if (position) throw std::runtime_error("FusionStep : position ambigue pour " + FILENAME);
                    position = &candidate;
                }
            }
            if (!position)
                throw std::runtime_error("FusionStep : position introuvable pour " + FILENAME);
            if (std::abs(position->rotationRadians) > ROTATION_TOLERANCE)
                throw std::runtime_error("FusionStep : rotations non prises en charge");
            const auto& level = tile->getLevel(0);
            if (level.width <= 0 || level.height <= 0)
                throw std::runtime_error("FusionStep : dimensions invalides");
            placements.push_back(TilePlacement{
                static_cast<int>(i), tile->getSource(),
                cv::Rect(cvRound(position->position.x), cvRound(position->position.y),
                         level.width, level.height), 1.0
            });
        }

        for (const auto& territory : territories) {
            if (territory.tileIndex < 0 ||
                territory.tileIndex >= static_cast<int>(tiles.size()))
                throw std::runtime_error("FusionStep : tileIndex hors limites");
        }

        const cv::Rect BOUNDS = computeGlobalBoundingBox(placements);
        if (BOUNDS.empty()) throw std::runtime_error("FusionStep : mosaique vide");
        const std::filesystem::path DIRECTORY = input.outputPath.empty()
            ? std::filesystem::path("fusion_output")
            : std::filesystem::path(input.outputPath);
        std::filesystem::create_directories(DIRECTORY);

        for (int y = BOUNDS.y; y < BOUNDS.y + BOUNDS.height; y += input.blockSize) {
            for (int x = BOUNDS.x; x < BOUNDS.x + BOUNDS.width; x += input.blockSize) {
                const cv::Rect BLOCK(x, y,
                    std::min(input.blockSize, BOUNDS.x + BOUNDS.width - x),
                    std::min(input.blockSize, BOUNDS.y + BOUNDS.height - y));
                cv::Mat rgb, provenance, mask;
                renderBlock(BLOCK, placements, territories, rgb, provenance, mask);
                const std::string SUFFIX = "_x" + std::to_string(x) +
                                           "_y" + std::to_string(y) + ".png";
                const bool OK =
                    cv::imwrite((DIRECTORY / ("rgb" + SUFFIX)).string(), rgb) &&
                    cv::imwrite((DIRECTORY / ("provenance" + SUFFIX)).string(), provenance) &&
                    cv::imwrite((DIRECTORY / ("mask" + SUFFIX)).string(), mask);
                if (!OK) throw std::runtime_error("FusionStep : ecriture bloc echouee");
                ++output.totalBlocks;
            }
        }
        output.mosaicPath = DIRECTORY.string();
        result["mosaicPath"] = output.mosaicPath;
        result["totalBlocks"] = output.totalBlocks;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Erreur FusionStep : " << e.what() << '\n';
        result["error"] = e.what();
        return false;
    }
}

} // namespace StepsNs
