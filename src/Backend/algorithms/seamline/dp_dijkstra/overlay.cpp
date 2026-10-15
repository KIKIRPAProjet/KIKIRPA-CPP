#include "algorithms/seamline/overlay.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include <stdexcept>

namespace seamlib {

    cv::Mat draw_seam_network(const cv::Mat& mosaic, const SeamGrid& grid, const std::map<std::string, SolveResult>& seams,
        const JunctionSet& junctions, const std::map<std::string, cv::Point2d>& origins, double scale) {
        if (mosaic.empty()) throw std::invalid_argument("draw_seam_network : mosaïque vide");
        if (!(scale > 0.0 && scale <= 1.0)) throw std::invalid_argument("draw_seam_network : scale doit être dans ]0, 1]");

        // Copie BGR 8 bits, réduite à l'échelle d'affichage.
        cv::Mat img;
        if (mosaic.depth() == CV_8U)
            img = mosaic;
        else
            mosaic.convertTo(img, CV_8U);
        if (img.channels() == 1) cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        else if (img.channels() == 4) cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
        if (scale < 1.0)
            cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_AREA);
        else
            img = img.clone();

        // Épaisseurs proportionnelles à la taille de la sortie, pour rester lisibles à toute échelle.
        const int thickness = std::clamp(int(std::lround(0.003 * std::max(img.cols, img.rows))), 1, 12);
        const int radius = 3 * thickness;
        const cv::Scalar red(0, 0, 255), yellow(0, 255, 255), black(0, 0, 0);

        // Point du solveur → pixel de la sortie. Adjacence V : solveur transposé.
        auto to_img = [&](const EdgeCost& e, bool vertical, const cv::Point2d& origin, const cv::Point& p) {
            const double sx = vertical ? p.y : p.x, sy = vertical ? p.x : p.y;
            return cv::Point(int(std::lround((origin.x + sx / e.scale) * scale)), int(std::lround((origin.y + sy / e.scale) * scale)));
        };

        for (const auto& [key, res] : seams) {
            const auto o = origins.find(key);
            const auto g = grid.edges.find(key);
            if (o == origins.end() || g == grid.edges.end() || res.path.empty()) continue;
            const bool vertical = key[0] == 'V';
            std::vector<cv::Point> poly;
            poly.reserve(res.path.size());
            for (const cv::Point& p : res.path) poly.push_back(to_img(g->second, vertical, o->second, p));
            cv::polylines(img, poly, false, red, thickness, cv::LINE_AA);

            // Les extrémités imposées sont les jonctions : on les repère depuis les Endpoints de l'adjacence.
            const Endpoints ep = edge_endpoints(grid, key, junctions);
            for (const std::optional<cv::Point>& j : { ep.start, ep.goal }) {
                if (!j) continue;
                const cv::Point c = to_img(g->second, vertical, o->second, *j);
                cv::circle(img, c, radius + 1, black, cv::FILLED, cv::LINE_AA);
                cv::circle(img, c, radius, yellow, cv::FILLED, cv::LINE_AA);
            }
        }
        return img;
    }

}  // namespace seamlib
