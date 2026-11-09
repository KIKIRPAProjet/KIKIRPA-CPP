// src/backend/algorithms/stitching/positioning/position_solver.cpp
#include "algorithms/stitching/positioning/position_solver.h"

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <cstddef>
#include <vector>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace PositioningNs {

    namespace {

        void validateOffsets(std::size_t tileCount, const std::vector<RelativeTilePosition>& offsets) {
            if (tileCount > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                offsets.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("position solver: too many tiles or constraints");
            for (auto& offset : offsets) {
                if (offset.tileIndexA >= tileCount || offset.tileIndexB >= tileCount || offset.tileIndexA == offset.tileIndexB)
                    throw std::invalid_argument("position solver: invalid tile indices");
                if (!std::isfinite(offset.translation.x) || !std::isfinite(offset.translation.y) || !std::isfinite(offset.rotationRadians))
                    throw std::invalid_argument("position solver: non-finite measurement");
            }
        }

        std::vector<bool> findConnectedTiles(std::size_t tileCount, const std::vector<RelativeTilePosition>& offsets,
                                             std::size_t referenceIndex) {
            std::vector<bool> connected(tileCount, false);
            connected[referenceIndex] = true;
            for (bool changed = true; changed;) {
                changed = false;
                for (auto& offset : offsets) {
                    if (connected[offset.tileIndexA] == connected[offset.tileIndexB]) continue;
                    connected[offset.tileIndexA] = true;
                    connected[offset.tileIndexB] = true;
                    changed = true;
                }
            }
            return connected;
        }

        struct EquationSystem {
            cv::Mat coefficients;
            cv::Mat measurements;
            std::vector<int> variableIndices;
        };

        EquationSystem buildEquations(const PositionSolution& solution, const std::vector<RelativeTilePosition>& offsets,
                                      std::size_t referenceIndex) {
            EquationSystem system;
            system.variableIndices.assign(solution.connected.size(), -1);
            int variableCount = 0;
            for (std::size_t index = 0; index < solution.connected.size(); ++index)
                if (index != referenceIndex && solution.connected[index]) system.variableIndices[index] = variableCount++;
            int rowCount = 0;
            for (auto& offset : offsets)
                if (solution.connected[offset.tileIndexA]) ++rowCount;
            system.coefficients = cv::Mat::zeros(rowCount, variableCount, CV_64F);
            system.measurements = cv::Mat::zeros(rowCount, 3, CV_64F);
            int rowIndex = 0;
            for (auto& offset : offsets) {
                if (!solution.connected[offset.tileIndexA]) continue;
                if (offset.tileIndexA != referenceIndex)
                    system.coefficients.at<double>(rowIndex, system.variableIndices[offset.tileIndexA]) = -1.0;
                if (offset.tileIndexB != referenceIndex)
                    system.coefficients.at<double>(rowIndex, system.variableIndices[offset.tileIndexB]) = 1.0;
                system.measurements.at<double>(rowIndex, 0) = offset.translation.x;
                system.measurements.at<double>(rowIndex, 1) = offset.translation.y;
                system.measurements.at<double>(rowIndex++, 2) = offset.rotationRadians;
            }
            return system;
        }

        void solveEquations(PositionSolution& solution, const EquationSystem& system) {
            if (system.coefficients.cols == 0) return;
            cv::Mat values;
            if (!cv::solve(system.coefficients, system.measurements, values, cv::DECOMP_SVD) || !cv::checkRange(values))
                throw std::runtime_error("position solver: least-squares solution failed");
            for (std::size_t index = 0; index < solution.connected.size(); ++index) {
                int variableIndex = system.variableIndices[index];
                if (variableIndex < 0) continue;
                solution.positions[index] = {values.at<double>(variableIndex, 0), values.at<double>(variableIndex, 1)};
                solution.rotations[index] = values.at<double>(variableIndex, 2);
            }
        }

    }

    PositionSolution solveRelativePositions(std::size_t tileCount, const std::vector<RelativeTilePosition>& offsets,
                                           std::size_t referenceIndex) {
        if (tileCount == 0 || referenceIndex >= tileCount)
            throw std::invalid_argument("position solver: invalid reference tile");
        validateOffsets(tileCount, offsets);
        PositionSolution solution;
        solution.connected = findConnectedTiles(tileCount, offsets, referenceIndex);
        solution.positions.resize(tileCount);
        solution.rotations.resize(tileCount, 0.0);
        solveEquations(solution, buildEquations(solution, offsets, referenceIndex));
        return solution;
    }

}
