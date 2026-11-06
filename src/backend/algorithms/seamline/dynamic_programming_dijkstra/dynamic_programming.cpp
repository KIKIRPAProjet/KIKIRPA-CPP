// src/backend/algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        const double INFINITE_DISTANCE = std::numeric_limits<double>::infinity();

        cv::Mat convertToDouble(const cv::Mat& cost) {
            CV_Assert(cost.channels() == 1 && !cost.empty());
            cv::Mat doubleCost;
            cost.convertTo(doubleCost, CV_64F);
            return doubleCost;
        }

        // Équivalent de _dp_fill_python : remplit les tables de distance et de parent des lignes startRow à goalRow.
        // startColumn < 0 signifie « départ libre sur toute la ligne startRow ».
        void fillDynamicProgrammingTables(const cv::Mat& cost, int startRow, int startColumn, int goalRow, cv::Mat& distances,
                                          cv::Mat& parents) {
            const int width = cost.cols;
            distances = cv::Mat(cost.size(), CV_64F, cv::Scalar(INFINITE_DISTANCE));
            parents = cv::Mat(cost.size(), CV_32S, cv::Scalar(-1));
            if (startColumn < 0)
                cost.row(startRow).copyTo(distances.row(startRow));
            else
                distances.at<double>(startRow, startColumn) = cost.at<double>(startRow, startColumn);

            for (int row = startRow + 1; row <= goalRow; ++row) {
                for (int column = 0; column < width; ++column) {
                    double bestDistance = INFINITE_DISTANCE;
                    int bestParentColumn = -1;
                    // Ordre column−1, column, column+1 : premier minimum gardé
                    for (int parentColumn = column - 1; parentColumn <= column + 1; ++parentColumn) {
                        if (0 <= parentColumn && parentColumn < width &&
                            std::isfinite(distances.at<double>(row - 1, parentColumn))) {
                            const double distance = distances.at<double>(row - 1, parentColumn) +
                                                    computeEdgeWeight(cost, row - 1, parentColumn, row, column);
                            if (distance < bestDistance) {
                                bestDistance = distance;
                                bestParentColumn = parentColumn;
                            }
                        }
                    }
                    distances.at<double>(row, column) = bestDistance;
                    parents.at<int>(row, column) = bestParentColumn;
                }
            }
        }

    }

    double computeEdgeWeight(const cv::Mat& cost, int row, int column, int neighbourRow, int neighbourColumn) {
        const double stepLength = (neighbourRow != row && neighbourColumn != column) ? SQRT2 : 1.0;
        return 0.5 * (cost.at<double>(row, column) + cost.at<double>(neighbourRow, neighbourColumn)) * stepLength;
    }

    PathResult findDynamicProgrammingPath(const cv::Mat& inputCost, const cv::Point* start, const cv::Point* goal) {
        const cv::Mat cost = convertToDouble(inputCost);
        const int height = cost.rows, width = cost.cols;
        const int startRow = start ? start->y : 0;
        const int startColumn = start ? start->x : -1;
        const int goalRow = goal ? goal->y : height - 1;
        if (startRow > goalRow) throw std::invalid_argument("dynamic programming requires start.y <= goal.y");

        cv::Mat distances, parents;
        fillDynamicProgrammingTables(cost, startRow, startColumn, goalRow, distances, parents);

        int endColumn = 0;
        if (goal) {
            endColumn = goal->x;
        } else {   // np.argmin : premier minimum
            const double* goalRowDistances = distances.ptr<double>(goalRow);
            for (int column = 1; column < width; ++column)
                if (goalRowDistances[column] < goalRowDistances[endColumn]) endColumn = column;
        }
        if (!std::isfinite(distances.at<double>(goalRow, endColumn)))
            throw std::runtime_error("no constrained dynamic programming path");

        PathResult result;
        int column = endColumn;
        for (int row = goalRow; row >= startRow; --row) {
            result.path.push_back(cv::Point(column, row));
            if (row > startRow) column = parents.at<int>(row, column);
        }
        std::reverse(result.path.begin(), result.path.end());
        result.objective = distances.at<double>(goalRow, endColumn);
        result.expandedCount = static_cast<long long>(goalRow - startRow + 1) * width;
        return result;
    }

    cv::Mat computeDynamicProgrammingDistanceFromTop(const cv::Mat& inputCost) {
        const cv::Mat cost = convertToDouble(inputCost);
        const int height = cost.rows, width = cost.cols;
        cv::Mat distances(cost.size(), CV_64F, cv::Scalar(INFINITE_DISTANCE));
        cost.row(0).copyTo(distances.row(0));
        for (int row = 1; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                double bestDistance = INFINITE_DISTANCE;
                for (int parentColumn = column - 1; parentColumn <= column + 1; ++parentColumn) {
                    if (0 <= parentColumn && parentColumn < width) {
                        const double distance = distances.at<double>(row - 1, parentColumn) +
                                                computeEdgeWeight(cost, row - 1, parentColumn, row, column);
                        if (distance < bestDistance) bestDistance = distance;
                    }
                }
                distances.at<double>(row, column) = bestDistance;
            }
        }
        return distances;
    }

}
