// src/backend/steps/dynamic_programming_dijkstra_step.cpp
#include "steps/dynamic_programming_dijkstra_step.h"

#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"
#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "core/config/pipeline_config.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <exception>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace StepsNs {

    using Json = nlohmann::json;
    namespace DP = DynamicProgrammingDijkstraNs;

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

    bool DynamicProgrammingDijkstraStepClass::execute() {
        output = {};
        result = Json::object();

        if (!dataStore) {
            result["error"] = "no data store is attached";
            return false;
        }

        DataNs::TileList tiles;
        std::vector<DataNs::TilePositionData> positions;
        DataNs::IntensityCorrection correction;
        DP::SolverMethod method;
        DP::CostFunction costFunction;
        int corridorRadius = 0;
        try {
            tiles = dataStore->get<DataNs::TileList>("tiles");
            positions = dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
            correction = dataStore->get<DataNs::IntensityCorrection>("intensityCorrection");
            method = DP::parseMethodName(getParameter("method").get<std::string>());
            costFunction = DP::parseCostFunctionName(getParameter("costFunction").get<std::string>());
            corridorRadius = getParameter("corridorRadius").get<int>();
        } catch (const std::exception& exception) {
            result["error"] = std::string("cannot read inputs or parameters: ") + exception.what();
            return false;
        }

        if (tiles.empty()) {
            result["error"] = "no tiles were provided";
            return false;
        }
        if (positions.size() != tiles.size()) {
            result["error"] = "positions do not match the tile list";
            return false;
        }
        if (correction.tileGains.size() != tiles.size()) {
            result["error"] = "intensityCorrection does not match the tile list";
            return false;
        }
        if (corridorRadius < 0) {
            result["error"] = "corridorRadius must be non-negative";
            return false;
        }

        std::unordered_map<int, std::size_t> tileIndexById;
        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
            if (!tiles[tileIndex] || !tileIndexById.emplace(tiles[tileIndex]->getId(), tileIndex).second) {
                result["error"] = "tile list contains a null tile or duplicate tile id";
                return false;
            }
            const cv::Point2d position = positions[tileIndex].position;
            if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
                result["error"] = "tile positions must be finite";
                return false;
            }
            const double gain = correction.tileGains[tileIndex];
            if (!std::isfinite(gain) || gain <= 0.0) {
                result["error"] = "tile gains must be finite and greater than zero";
                return false;
            }
        }

        DP::SeamFinderClass seamFinder(costFunction, DP::CostMapParameters(), method);
        seamFinder.corridorRadius = corridorRadius;

        for (std::size_t tileIndexA = 0; tileIndexA < tiles.size(); ++tileIndexA) {
            const DataNs::TileClass& tileA = *tiles[tileIndexA];
            const std::pair<DataNs::TileClass*, DataNs::Direction> neighbours[] = {
                {tileA.getRight(), DataNs::Direction::Right},
                {tileA.getBottom(), DataNs::Direction::Bottom}
            };

            cv::Mat imageA;
            for (const auto& [tileBPointer, direction] : neighbours) {
                if (!tileBPointer) continue;

                const auto tileIndexBEntry = tileIndexById.find(tileBPointer->getId());
                if (tileIndexBEntry == tileIndexById.end()) {
                    result["error"] = "a tile neighbour is missing from the tile list";
                    return false;
                }
                const std::size_t tileIndexB = tileIndexBEntry->second;
                if (imageA.empty()) imageA = cv::imread(tileA.getSource().string(), cv::IMREAD_UNCHANGED);
                const cv::Mat imageB = cv::imread(tileBPointer->getSource().string(), cv::IMREAD_UNCHANGED);
                if (imageA.empty() || imageB.empty()) {
                    result["error"] = "cannot read " + (imageA.empty() ? tileA.getSource() : tileBPointer->getSource()).string();
                    return false;
                }
                if (imageA.type() != imageB.type() || (imageA.channels() != 1 && imageA.channels() != 3) ||
                    (imageA.depth() != CV_8U && imageA.depth() != CV_16U && imageA.depth() != CV_32F)) {
                    result["error"] = "neighbouring tiles must have matching grayscale or three-channel image types";
                    return false;
                }

                const cv::Point2d translation = positions[tileIndexB].position - positions[tileIndexA].position;
                const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
                    imageA.size(), imageB.size(), translation);
                if (areaA.empty() || areaB.empty()) {
                    result["error"] = "neighbouring tiles do not overlap";
                    return false;
                }

                cv::Mat correctedA, correctedB;
                imageA(areaA).convertTo(correctedA, imageA.type(), 1.0 / correction.tileGains[tileIndexA]);
                imageB(areaB).convertTo(correctedB, imageB.type(), 1.0 / correction.tileGains[tileIndexB]);
                if (correctedA.channels() == 3) {
                    cv::cvtColor(correctedA, correctedA, cv::COLOR_BGR2RGB);
                    cv::cvtColor(correctedB, correctedB, cv::COLOR_BGR2RGB);
                }

                cv::Mat solverOverlapA, solverOverlapB;
                if (direction == DataNs::Direction::Bottom) {
                    cv::transpose(correctedA, solverOverlapA);
                    cv::transpose(correctedB, solverOverlapB);
                } else {
                    solverOverlapA = std::move(correctedA);
                    solverOverlapB = std::move(correctedB);
                }

                std::vector<cv::Point> path;
                try {
                    path = seamFinder.computeSeam(solverOverlapA, solverOverlapB);
                } catch (const std::exception& exception) {
                    result["error"] = std::string("seamline computation failed: ") + exception.what();
                    return false;
                }
                if (path.empty()) {
                    result["error"] = "seamline computation returned an empty path";
                    return false;
                }

                DataNs::Seamline seamline;
                seamline.tileIdA = tileA.getId();
                seamline.tileIdB = tileBPointer->getId();
                seamline.direction = direction;
                seamline.points.reserve(path.size());
                for (const cv::Point& pathPoint : path) {
                    const cv::Point2d localPoint = direction == DataNs::Direction::Bottom
                        ? cv::Point2d(pathPoint.y, pathPoint.x)
                        : cv::Point2d(pathPoint.x, pathPoint.y);
                    seamline.points.push_back(positions[tileIndexA].position +
                                              cv::Point2d(areaA.x + localPoint.x, areaA.y + localPoint.y));
                }
                output.seamlines.push_back(std::move(seamline));
            }
        }

        result["seamlines"] = output.seamlines.size();
        result["method"] = DP::getMethodName(method);
        result["costFunction"] = DP::getCostFunctionName(costFunction);
        dataStore->replace("seamlines", output.seamlines);
        return true;
    }

}
