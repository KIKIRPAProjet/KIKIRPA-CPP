// src/backend/steps/graph_cut_step.cpp
#include "steps/graph_cut_step.h"

#include "algorithms/seamline/graph_cut/graph_cut_seam_finder.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/unicode_path.h"
#include "core/step/step.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <cstddef>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace StepsNs {

    namespace {

        // La carte de provenance est sur 8 bits et réserve une valeur à « aucune tuile » : il reste NO_TILE tuiles possibles.
        constexpr std::size_t MAX_TILE_COUNT = static_cast<std::size_t>(GraphCutNs::NO_TILE) - 1;


        // Contrôles : au moins une tuile, pas trop de tuiles, une position par tuile, facteur de réduction valide.
        // Renvoie false au premier problème, avec la cause dans result["error"].
        bool checkInputs(const GraphCutInput& resolvedInput, StepNs::Json& result) {
            if (resolvedInput.tilePaths.empty()) {
                result["error"] = "No tile provided for graph cut step";
                return false;
            }
            if (resolvedInput.tilePaths.size() > MAX_TILE_COUNT) {
                result["error"] = "Too many tiles provided for graph cut step (got " +
                                  std::to_string(resolvedInput.tilePaths.size()) + ", max " + std::to_string(MAX_TILE_COUNT) + ")";
                return false;
            }
            if (resolvedInput.positions.size() != resolvedInput.tilePaths.size()) {
                result["error"] = "The number of positions (" + std::to_string(resolvedInput.positions.size()) +
                                  ") doesn't match the number of tile paths (" + std::to_string(resolvedInput.tilePaths.size()) + ")";
                return false;
            }
            if (resolvedInput.scale <= 0.0 || resolvedInput.scale > 1.0) {
                result["error"] = "Reduction scale must be greater than 0 and at most 1, got " + std::to_string(resolvedInput.scale);
                return false;
            }
            return true;
        }

        // Positions remises dans l'ordre des tuiles, chacune associée à sa tuile par TileClass::getId(). Lève
        // std::invalid_argument si une tuile n'a pas de position.
        std::vector<DataNs::TilePositionData> orderPositionsByTile(const DataNs::TileList& tiles,
                                                                   const std::vector<DataNs::TilePositionData>& positions) {
            std::map<int, const DataNs::TilePositionData*> positionByTileId;
            for (const DataNs::TilePositionData& tilePosition : positions) positionByTileId[tilePosition.tileId] = &tilePosition;
            std::vector<DataNs::TilePositionData> orderedPositions;
            for (const auto& tile : tiles) {
                const auto entry = positionByTileId.find(tile->getId());
                if (entry == positionByTileId.end())
                    throw std::invalid_argument("no position provided for tile " + std::to_string(tile->getId()));
                orderedPositions.push_back(*entry->second);
            }
            return orderedPositions;
        }

        // Prétraitement : tailles des tuiles, coins dans la mosaïque et masques réduits.
        // Renvoie false en cas d'échec, avec la cause dans result["error"].
        bool preprocessTiles(const GraphCutInput& resolvedInput, GraphCutNs::PreprocessedTiles& tiles, StepNs::Json& result) {
            GraphCutNs::ImagePretreatmentClass imagePretreatment;
            const int status = imagePretreatment.processImages(resolvedInput.positions, resolvedInput.tilePaths, resolvedInput.scale,
                                                               tiles);
            if (status < 0) {
                result["error"] = "Image pretreatment error: " + imagePretreatment.getErrorMessage();
                return false;
            }
            return true;
        }

    }

    // Déclare l'identité du step : son nom, son paramètre `scale` (valeur par défaut = celle de l'entrée), les données
    // qu'il lit (tuiles et positions) et celle qu'il produit (territoires). Le pipeline s'en sert pour vérifier l'enchaînement.
    GraphCutStepClass::GraphCutStepClass(const GraphCutInput& initialInput) : input(initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "graphcut";
        stepConfig.parameters = {{"float", "scale", initialInput.scale}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"Positions", "positions"}};
        stepConfig.outputs = {{"Territories", "territories"}};
        configJson = stepConfig;
    }

    bool GraphCutStepClass::readInputs(GraphCutInput& resolvedInput) {
        resolvedInput = input;
        try {
            const bool hasStoredTiles = dataStore && dataStore->has("tiles");
            if (hasStoredTiles) {
                resolvedInput.tilePaths.clear();
                // L'algorithme relit les images depuis les fichiers d'origine.
                for (const auto& tile : dataStore->get<DataNs::TileList>("tiles"))
                    resolvedInput.tilePaths.push_back(IONs::pathToUtf8(tile->getSource()));
            }
            if (dataStore && dataStore->has("positions")) {
                const auto& positions = dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
                // Une position par tuile, dans l'ordre des tuiles : c'est ce qu'attend le prétraitement. Si les nombres
                // diffèrent, les positions sont gardées telles quelles et checkInputs signale l'écart.
                const bool canOrder = hasStoredTiles && positions.size() == resolvedInput.tilePaths.size();
                resolvedInput.positions = canOrder ? orderPositionsByTile(dataStore->get<DataNs::TileList>("tiles"), positions)
                                                   : positions;
            }
            resolvedInput.scale = getParameter("scale").get<double>();
        } catch (const std::exception& exception) {
            result["error"] = std::string("Impossible to read inputs: ") + exception.what();
            return false;
        }
        return true;
    }

    bool GraphCutStepClass::execute() {
        GraphCutInput resolvedInput;
        if (!readInputs(resolvedInput))
            return false;
        if (!checkInputs(resolvedInput, result))
            return false;
        GraphCutNs::PreprocessedTiles tiles;
        if (!preprocessTiles(resolvedInput, tiles, result))
            return false;
        GraphCutNs::GraphCutSeamFinderClass graphCut;         // Coupe de graphe : produit la carte de provenance `indexMatrix` (la tuile choisie pour chaque pixel de la mosaïque).
        cv::Mat indexMatrix;
        const GraphCutNs::FullResolutionGeometry fullResolution{tiles.tileSizes, tiles.originalCorners};
        GraphCutNs::ReducedGrid reducedGrid{resolvedInput.scale, tiles.cornersSmall, tiles.masksUmatSmall};
        const bool isCutDone = graphCut.computeSeamline(resolvedInput.tilePaths, fullResolution, reducedGrid, indexMatrix);
        if (!isCutDone) {
            result["error"] = "Graph cut computation error: " + graphCut.getErrorMessage();
            return false;
        }
        // Territoires : les polygones de chaque tuile sont extraits de la carte de provenance, puis rangés dans le
        // DataStoreClass pour les steps suivants.
        std::vector<cv::Rect> tileRects;
        tileRects.reserve(tiles.tileSizes.size());
        for (std::size_t tileIndex = 0; tileIndex < tiles.tileSizes.size(); ++tileIndex)
            tileRects.emplace_back(tiles.originalCorners[tileIndex], tiles.tileSizes[tileIndex]);
        output.territories = graphCut.extractPolygons(indexMatrix, tileRects);
        if (dataStore)
            dataStore->replace("territories", output.territories);
        result["territories"] = output.territories.size();
        return true;
    }

}
