// src/backend/algorithms/seamline/dynamic_programming_dijkstra/overlay.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/overlay.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    cv::Mat drawSeamNetwork(const cv::Mat& mosaic, const SeamGrid& grid, const std::map<std::string, SolveResult>& seams,
                            const JunctionSet& junctions, const std::map<std::string, cv::Point2d>& origins, double scale) {
        if (mosaic.empty()) throw std::invalid_argument("drawSeamNetwork: empty mosaic");
        if (!(scale > 0.0 && scale <= 1.0)) throw std::invalid_argument("drawSeamNetwork: scale must be in ]0, 1]");

        // Copie BGR 8 bits, réduite à l'échelle d'affichage.
        cv::Mat image;
        if (mosaic.depth() == CV_8U)
            image = mosaic;
        else
            mosaic.convertTo(image, CV_8U);
        if (image.channels() == 1) cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
        else if (image.channels() == 4) cv::cvtColor(image, image, cv::COLOR_BGRA2BGR);
        if (scale < 1.0)
            cv::resize(image, image, cv::Size(), scale, scale, cv::INTER_AREA);
        else
            image = image.clone();

        // Épaisseurs proportionnelles à la taille de la sortie, pour rester lisibles à toute échelle.
        const int thickness = std::clamp(int(std::lround(0.003 * std::max(image.cols, image.rows))), 1, 12);
        const int radius = 3 * thickness;
        const cv::Scalar red(0, 0, 255), yellow(0, 255, 255), black(0, 0, 0);

        // Point du solveur → pixel de la sortie. Adjacence V : solveur transposé.
        auto toImage = [&](const EdgeCost& edge, bool isVertical, const cv::Point2d& origin, const cv::Point& point) {
            const double solverX = isVertical ? point.y : point.x, solverY = isVertical ? point.x : point.y;
            return cv::Point(int(std::lround((origin.x + solverX / edge.scale) * scale)),
                             int(std::lround((origin.y + solverY / edge.scale) * scale)));
        };

        for (const auto& [key, seam] : seams) {
            const auto origin = origins.find(key);
            const auto edge = grid.edges.find(key);
            if (origin == origins.end() || edge == grid.edges.end() || seam.path.empty()) continue;
            const bool isVertical = key[0] == 'V';
            std::vector<cv::Point> polyline;
            polyline.reserve(seam.path.size());
            for (const cv::Point& point : seam.path) polyline.push_back(toImage(edge->second, isVertical, origin->second, point));
            cv::polylines(image, polyline, false, red, thickness, cv::LINE_AA);

            // Les extrémités imposées sont les jonctions : on les repère depuis les Endpoints de l'adjacence.
            const Endpoints endpoints = computeEdgeEndpoints(grid, key, junctions);
            for (const std::optional<cv::Point>& junctionPoint : {endpoints.start, endpoints.goal}) {
                if (!junctionPoint) continue;
                const cv::Point center = toImage(edge->second, isVertical, origin->second, *junctionPoint);
                cv::circle(image, center, radius + 1, black, cv::FILLED, cv::LINE_AA);
                cv::circle(image, center, radius, yellow, cv::FILLED, cv::LINE_AA);
            }
        }
        return image;
    }

}
