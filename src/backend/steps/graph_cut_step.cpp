// src/backend/steps/graph_cut_step.cpp
#include "steps/graph_cut_step.h"

#include "algorithms/seamline/graph_cut/graph_cut_seam_finder.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <vector>

namespace StepsNs {

    namespace {

        // La carte de provenance est sur 8 bits et réserve une valeur à « aucune tuile » : il reste NO_TILE tuiles possibles.
        constexpr std::size_t MAX_TILE_COUNT = static_cast<std::size_t>(SeamlineNs::NO_TILE) - 1;

        // Résultat du prétraitement des images : tailles, coins dans la mosaïque (pleine résolution et réduits) et
        // masques réduits.
        struct PreprocessedTiles {
            std::vector<cv::Size> tileSizes;
            std::vector<cv::Point> originalCorners;
            std::vector<cv::Point> cornersSmall;
            std::vector<cv::UMat> masksUmatSmall;
        };

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

        // Prétraitement : tailles des tuiles, coins dans la mosaïque et masques réduits.
        // Renvoie false en cas d'échec, avec la cause dans result["error"].
        bool preprocessTiles(const GraphCutInput& resolvedInput, PreprocessedTiles& tiles, StepNs::Json& result) {
            SeamlineNs::ImagePretreatment imagePretreatment;
            const int status = imagePretreatment.processImages(resolvedInput.positions, resolvedInput.tilePaths, resolvedInput.scale,
                                                               tiles.tileSizes, tiles.masksUmatSmall, tiles.originalCorners,
                                                               tiles.cornersSmall);
            if (status < 0) {
                result["error"] = "Error while image pretreatment";
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
            if (dataStore && dataStore->has("tiles")) {
                resolvedInput.tilePaths.clear();
                // L'algorithme relit les images depuis les fichiers d'origine.
                for (const auto& tile : dataStore->get<DataNs::TileList>("tiles"))
                    resolvedInput.tilePaths.push_back(tile->getSource().string());
            }
            if (dataStore && dataStore->has("positions"))
                resolvedInput.positions = dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
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

        PreprocessedTiles tiles;
        if (!preprocessTiles(resolvedInput, tiles, result))
            return false;

        // Coupe de graphe : produit la carte de provenance `indexMatrix` (la tuile choisie pour chaque pixel de la mosaïque).
        SeamlineNs::GraphCutClass graphCut;
        cv::Mat indexMatrix;
        
        bool isCutDone = graphCut.computeSeamline(resolvedInput.tilePaths, tiles.tileSizes, tiles.originalCorners, resolvedInput.scale,
                                                 tiles.cornersSmall, tiles.masksUmatSmall, indexMatrix);
        if (!isCutDone) {
            result["error"] = "Graph cut computation error: " + graphCut.getErrorMessage();
            return false;
        }

        // Territoires : les polygones de chaque tuile sont extraits de la carte de provenance, puis rangés dans le
        // DataStoreClass pour les steps suivants.
        output.territories = graphCut.extractPolygons(indexMatrix, resolvedInput.tilePaths.size());
        if (dataStore)
            dataStore->replace("territories", output.territories);
        result["territories"] = output.territories.size();
        return true;
    }

}
