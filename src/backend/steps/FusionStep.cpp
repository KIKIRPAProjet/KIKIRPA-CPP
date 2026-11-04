#include "steps/FusionStep.h"

#include "core/config/pipeline_config.h"
#include "core/step/data_store.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace StepsNs {

FusionStepClass::FusionStepClass(const FusionInput& initialInput)
    : input_(initialInput) {
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
    stepConfig.outputs = {}; // Sortie sur disque ; contrat de sortie DataStore à définir avec l'équipe.
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
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, cv::Scalar(255));
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

void FusionStepClass::renderBlock(
    const cv::Rect& block,
    const std::vector<TilePlacement>& placements,
    const std::vector<DataNs::TileTerritory>& territories,
    cv::Mat& outBlockRgb,
    cv::Mat& outBlockProvenance,
    cv::Mat& outBlockMask) const {

    // OpenCV imread charge en BGR ; le nom outBlockRgb est historique.
    outBlockRgb = cv::Mat::zeros(block.size(), CV_16UC3);
    outBlockProvenance = cv::Mat::zeros(block.size(), CV_16UC1);
    outBlockMask = cv::Mat::zeros(block.size(), CV_8UC1);

    std::map<int, const DataNs::TileTerritory*> territoryByIndex;
    for (const auto& territory : territories)
        territoryByIndex[territory.tileIndex] = &territory;

    for (const auto& tile : placements) {
        auto territoryIt = territoryByIndex.find(tile.index);
        if (territoryIt == territoryByIndex.end()) continue;
        const cv::Rect intersection = block & tile.globalBox;
        if (intersection.empty()) continue;

        cv::Mat source = cv::imread(tile.path.string(), cv::IMREAD_UNCHANGED);
        if (source.empty())
            throw std::runtime_error("Impossible de lire la tuile : " + tile.path.string());
        if (source.type() != CV_16UC3)
            throw std::runtime_error("Tuile non CV_16UC3 : " + tile.path.string());
        if (source.size() != tile.globalBox.size())
            throw std::runtime_error("Dimensions inattendues : " + tile.path.string());

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
        outBlockMask(dstRect).setTo(cv::Scalar(255), maskRoi);
    }
}

bool FusionStepClass::execute() {
    output_ = {};
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
        if (input_.blockSize <= 0)
            throw std::runtime_error("FusionStep : blockSize doit etre positif");
        if (input_.enableBlending)
            throw std::runtime_error("FusionStep : blending pas encore implemente");

        // L'identifiant de position est de forme rLL_cCC. On le retrouve
        // dans le nom de fichier source, sans supposer l'ordre des positions.
        std::vector<TilePlacement> placements;
        placements.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto& tile = tiles[i];
            if (!tile) throw std::runtime_error("FusionStep : tuile nulle");
            const std::string filename = tile->getSource().filename().string();
            const DataNs::TilePositionData* position = nullptr;
            for (const auto& candidate : positions) {
                if (filename.find(candidate.tileId) != std::string::npos) {
                    if (position) throw std::runtime_error("FusionStep : position ambigue pour " + filename);
                    position = &candidate;
                }
            }
            if (!position)
                throw std::runtime_error("FusionStep : position introuvable pour " + filename);
            if (std::abs(position->rotationRadians) > 1e-9)
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

        const cv::Rect bounds = computeGlobalBoundingBox(placements);
        if (bounds.empty()) throw std::runtime_error("FusionStep : mosaique vide");
        const std::filesystem::path directory = input_.outputPath.empty()
            ? std::filesystem::path("fusion_output")
            : std::filesystem::path(input_.outputPath);
        std::filesystem::create_directories(directory);

        for (int y = bounds.y; y < bounds.y + bounds.height; y += input_.blockSize) {
            for (int x = bounds.x; x < bounds.x + bounds.width; x += input_.blockSize) {
                const cv::Rect block(x, y,
                    std::min(input_.blockSize, bounds.x + bounds.width - x),
                    std::min(input_.blockSize, bounds.y + bounds.height - y));
                cv::Mat rgb, provenance, mask;
                renderBlock(block, placements, territories, rgb, provenance, mask);
                const std::string suffix = "_x" + std::to_string(x) +
                                           "_y" + std::to_string(y) + ".png";
                const bool ok =
                    cv::imwrite((directory / ("rgb" + suffix)).string(), rgb) &&
                    cv::imwrite((directory / ("provenance" + suffix)).string(), provenance) &&
                    cv::imwrite((directory / ("mask" + suffix)).string(), mask);
                if (!ok) throw std::runtime_error("FusionStep : ecriture bloc echouee");
                ++output_.totalBlocks;
            }
        }
        output_.mosaicPath = directory.string();
        result["mosaicPath"] = output_.mosaicPath;
        result["totalBlocks"] = output_.totalBlocks;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Erreur FusionStep : " << e.what() << '\n';
        result["error"] = e.what();
        return false;
    }
}

} // namespace StepsNs
