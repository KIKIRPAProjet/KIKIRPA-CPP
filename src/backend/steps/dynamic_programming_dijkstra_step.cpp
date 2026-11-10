// src/backend/steps/dynamic_programming_dijkstra_step.cpp
#include "steps/dynamic_programming_dijkstra_step.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;
    namespace DP = DynamicProgrammingDijkstraNs;

    struct DynamicProgrammingDijkstraData {
        DataNs::TileList tiles;
        std::vector<DataNs::TilePositionData> positions;   // une par tuile, dans l'ordre de tiles
        DataNs::IntensityCorrection correction;            // un gain par tuile, dans l'ordre de tiles
        DP::SolverMethod method = DP::SolverMethod::DynamicProgramming;
        DP::CostFunction costFunction = DP::CostFunction::ColorGradient;
        int corridorRadius = 0;
    };

    namespace {

        using TileIndexById = std::unordered_map<int, std::size_t>;

        // Une paire de voisines à traiter : A, sa voisine B, leur place dans tiles et la position de B par rapport à A.
        struct NeighbourPair {
            const DataNs::TileClass& tileA;
            std::size_t indexA;
            const DataNs::TileClass& tileB;
            std::size_t indexB;
            DataNs::Direction direction;
        };

        // Images d'une paire. L'image de A est gardée pour ses deux voisines (droite puis dessous).
        struct PairImages {
            cv::Mat imageA;
            cv::Mat imageB;
        };

        // Recouvrements prêts pour le solveur (corrigés des gains, en RVB, transposés pour une paire verticale),
        // et origine du recouvrement dans A.
        struct SolverOverlaps {
            cv::Mat overlapA;
            cv::Mat overlapB;
            cv::Rect areaA;
        };

        // Ce qui ne change pas d'une paire à l'autre.
        struct SeamContext {
            const DynamicProgrammingDijkstraData& inputs;
            DP::SeamFinderClass& seamFinder;
        };

        // Contrôles globaux des entrées. Renvoie la cause d'un échec, vide si tout est en ordre.
        std::string validateInputs(const DynamicProgrammingDijkstraData& inputs) {
            if (inputs.tiles.empty()) return "no tiles were provided";
            if (inputs.positions.size() != inputs.tiles.size()) return "positions do not match the tile list";
            if (inputs.correction.tileGains.size() != inputs.tiles.size()) return "intensityCorrection does not match the tile list";
            if (inputs.corridorRadius < 0) return "corridorRadius must be non-negative";
            return {};
        }

        // Indexe les tuiles par identifiant et contrôle leur position et leur gain.
        std::string indexTiles(const DynamicProgrammingDijkstraData& inputs, TileIndexById& tileIndexById) {
            for (std::size_t tileIndex = 0; tileIndex < inputs.tiles.size(); ++tileIndex) {
                if (!inputs.tiles[tileIndex] || !tileIndexById.emplace(inputs.tiles[tileIndex]->getId(), tileIndex).second)
                    return "tile list contains a null tile or duplicate tile id";
                const cv::Point2d position = inputs.positions[tileIndex].position;
                if (!std::isfinite(position.x) || !std::isfinite(position.y)) return "tile positions must be finite";
                const double gain = inputs.correction.tileGains[tileIndex];
                if (!std::isfinite(gain) || gain <= 0.0) return "tile gains must be finite and greater than zero";
            }
            return {};
        }

        // Lit les images de la paire (A seulement si elle n'est pas déjà lue) et contrôle leur type.
        std::string readPairImages(const NeighbourPair& pair, PairImages& images) {
            if (images.imageA.empty()) images.imageA = cv::imread(pair.tileA.getSource().string(), cv::IMREAD_UNCHANGED);
            images.imageB = cv::imread(pair.tileB.getSource().string(), cv::IMREAD_UNCHANGED);
            if (images.imageA.empty() || images.imageB.empty())
                return "cannot read " + (images.imageA.empty() ? pair.tileA.getSource() : pair.tileB.getSource()).string();
            const int depth = images.imageA.depth();
            if (images.imageA.type() != images.imageB.type() || (images.imageA.channels() != 1 && images.imageA.channels() != 3) ||
                (depth != CV_8U && depth != CV_16U && depth != CV_32F))
                return "neighbouring tiles must have matching grayscale or three-channel image types";
            return {};
        }

        // Découpe le recouvrement dans les deux images, le divise par les gains et le prépare pour le solveur,
        // qui cherche toujours un chemin du haut vers le bas.
        std::string cutOverlaps(const DynamicProgrammingDijkstraData& inputs, const NeighbourPair& pair, const PairImages& images,
                                SolverOverlaps& overlaps) {
            const cv::Point2d translation = inputs.positions[pair.indexB].position - inputs.positions[pair.indexA].position;
            const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                images.imageA.size(), images.imageB.size(), translation);
            if (areaA.empty() || areaB.empty()) return "neighbouring tiles do not overlap";

            cv::Mat correctedA;
            cv::Mat correctedB;
            images.imageA(areaA).convertTo(correctedA, images.imageA.type(), 1.0 / inputs.correction.tileGains[pair.indexA]);
            images.imageB(areaB).convertTo(correctedB, images.imageB.type(), 1.0 / inputs.correction.tileGains[pair.indexB]);
            if (correctedA.channels() == 3) {
                cv::cvtColor(correctedA, correctedA, cv::COLOR_BGR2RGB);
                cv::cvtColor(correctedB, correctedB, cv::COLOR_BGR2RGB);
            }
            if (pair.direction == DataNs::Direction::Bottom) {
                cv::transpose(correctedA, overlaps.overlapA);
                cv::transpose(correctedB, overlaps.overlapB);
            } else {
                overlaps.overlapA = std::move(correctedA);
                overlaps.overlapB = std::move(correctedB);
            }
            overlaps.areaA = areaA;
            return {};
        }

        // Ramène le chemin du solveur dans le repère de la mosaïque. originInMosaic = coin du recouvrement de A.
        DataNs::Seamline toSeamline(const NeighbourPair& pair, const std::vector<cv::Point>& path, const cv::Point2d& originInMosaic) {
            DataNs::Seamline seamline;
            seamline.tileIdA = pair.tileA.getId();
            seamline.tileIdB = pair.tileB.getId();
            seamline.direction = pair.direction;
            seamline.points.reserve(path.size());
            for (const cv::Point& pathPoint : path) {
                const cv::Point2d localPoint = pair.direction == DataNs::Direction::Bottom ? cv::Point2d(pathPoint.y, pathPoint.x)
                                                                                           : cv::Point2d(pathPoint.x, pathPoint.y);
                seamline.points.push_back(originInMosaic + localPoint);
            }
            return seamline;
        }

        // Calcule la couture d'une paire. Renvoie la cause d'un échec, vide si la couture est calculée.
        std::string computePairSeamline(const SeamContext& context, const NeighbourPair& pair, PairImages& images,
                                        DataNs::Seamline& seamline) {
            std::string error = readPairImages(pair, images);
            SolverOverlaps overlaps;
            if (error.empty()) error = cutOverlaps(context.inputs, pair, images, overlaps);
            if (!error.empty()) return error;

            std::vector<cv::Point> path;
            try {
                path = context.seamFinder.computeSeam(overlaps.overlapA, overlaps.overlapB);
            } catch (const std::exception& exception) {
                return std::string("seamline computation failed: ") + exception.what();
            }
            if (path.empty()) return "seamline computation returned an empty path";
            const cv::Point2d originInMosaic =
                context.inputs.positions[pair.indexA].position + cv::Point2d(overlaps.areaA.x, overlaps.areaA.y);
            seamline = toSeamline(pair, path, originInMosaic);
            return {};
        }

        // Une couture par tuile et par voisine de droite ou du dessous : chaque paire n'est traitée qu'une fois.
        std::string computeSeamlines(const DynamicProgrammingDijkstraData& inputs, const TileIndexById& tileIndexById,
                                     DataNs::Seamlines& seamlines) {
            DP::SeamFinderClass seamFinder(inputs.costFunction, DP::CostMapParameters(), inputs.method);
            seamFinder.corridorRadius = inputs.corridorRadius;
            const SeamContext context{inputs, seamFinder};
            for (std::size_t indexA = 0; indexA < inputs.tiles.size(); ++indexA) {
                const DataNs::TileClass& tileA = *inputs.tiles[indexA];
                const std::pair<const DataNs::TileClass*, DataNs::Direction> neighbours[] = {
                    {tileA.getRight(), DataNs::Direction::Right}, {tileA.getBottom(), DataNs::Direction::Bottom}};
                PairImages images;
                for (const auto& [tileB, direction] : neighbours) {
                    if (!tileB) continue;
                    const auto entryB = tileIndexById.find(tileB->getId());
                    if (entryB == tileIndexById.end()) return "a tile neighbour is missing from the tile list";
                    DataNs::Seamline seamline;
                    const std::string error =
                        computePairSeamline(context, {tileA, indexA, *tileB, entryB->second, direction}, images, seamline);
                    if (!error.empty()) return error;
                    seamlines.push_back(std::move(seamline));
                }
            }
            return {};
        }

    }

    DynamicProgrammingDijkstraStepClass::DynamicProgrammingDijkstraStepClass(const DynamicProgrammingDijkstraInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "dynamic-programming-dijkstra";
        stepConfig.parameters = {{"string", "method", initialInput.method},
                                 {"string", "costFunction", initialInput.costFunction},
                                 {"int", "corridorRadius", initialInput.corridorRadius}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"Positions", "positions"}, {"IntensityCorrection", "intensityCorrection"}};
        stepConfig.outputs = {{"Seamlines", "seamlines"}};
        configJson = stepConfig;
    }

    std::string DynamicProgrammingDijkstraStepClass::readInputs(DynamicProgrammingDijkstraData& inputs) const {
        if (!dataStore) return "no data store is attached";
        try {
            inputs.tiles = dataStore->get<DataNs::TileList>("tiles");
            inputs.positions = dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
            inputs.correction = dataStore->get<DataNs::IntensityCorrection>("intensityCorrection");
            inputs.method = DP::parseMethodName(getParameter("method").get<std::string>());
            inputs.costFunction = DP::parseCostFunctionName(getParameter("costFunction").get<std::string>());
            inputs.corridorRadius = getParameter("corridorRadius").get<int>();
        } catch (const std::exception& exception) {
            return std::string("cannot read inputs or parameters: ") + exception.what();
        }
        return {};
    }

    // Étapes : 1. lire et contrôler les entrées ; 2. calculer une couture par paire de voisines ; 3. publier les coutures.
    bool DynamicProgrammingDijkstraStepClass::execute() {
        output = {};
        result = Json::object();

        DynamicProgrammingDijkstraData inputs;
        TileIndexById tileIndexById;
        std::string error = readInputs(inputs);
        if (error.empty()) error = validateInputs(inputs);
        if (error.empty()) error = indexTiles(inputs, tileIndexById);
        if (error.empty()) error = computeSeamlines(inputs, tileIndexById, output.seamlines);
        if (!error.empty()) {
            result["error"] = error;
            return false;
        }

        result["seamlines"] = output.seamlines.size();
        result["method"] = DP::getMethodName(inputs.method);
        result["costFunction"] = DP::getCostFunctionName(inputs.costFunction);
        dataStore->replace("seamlines", output.seamlines);
        return true;
    }

}
