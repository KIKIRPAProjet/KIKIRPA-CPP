#include "seamlib/dynamic_programming.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace seamlib {

    namespace {

        const double INF = std::numeric_limits<double>::infinity();

        cv::Mat to_f64(const cv::Mat& cost) {
            CV_Assert(cost.channels() == 1 && !cost.empty());
            cv::Mat c;
            cost.convertTo(c, CV_64F);
            return c;
        }

        // Équivalent de _dp_fill_python : remplit les tables de distance et de parent
        // des lignes sy à gy. sx < 0 signifie « départ libre sur toute la ligne sy ».
        void dp_fill(const cv::Mat& cost, int sy, int sx, int gy, cv::Mat& dist, cv::Mat& parent) {
            const int w = cost.cols;
            dist = cv::Mat(cost.size(), CV_64F, cv::Scalar(INF));
            parent = cv::Mat(cost.size(), CV_32S, cv::Scalar(-1));
            if (sx < 0)
                cost.row(sy).copyTo(dist.row(sy));
            else
                dist.at<double>(sy, sx) = cost.at<double>(sy, sx);

            for (int y = sy + 1; y <= gy; ++y) {
                for (int x = 0; x < w; ++x) {
                    double best = INF;
                    int bx = -1;
                    for (int px = x - 1; px <= x + 1; ++px) {     // ordre x−1, x, x+1 : premier minimum gardé
                        if (0 <= px && px < w && std::isfinite(dist.at<double>(y - 1, px))) {
                            const double val = dist.at<double>(y - 1, px) + edge_weight(cost, y - 1, px, y, x);
                            if (val < best) {
                                best = val;
                                bx = px;
                            }
                        }
                    }
                    dist.at<double>(y, x) = best;
                    parent.at<int>(y, x) = bx;
                }
            }
        }

    }  // namespace

    double edge_weight(const cv::Mat& cost, int y, int x, int ny, int nx) {
        const double step = (ny != y && nx != x) ? SQRT2 : 1.0;
        return 0.5 * (cost.at<double>(y, x) + cost.at<double>(ny, nx)) * step;
    }

    PathResult dp_constrained(const cv::Mat& cost_in, const cv::Point* start, const cv::Point* goal) {
        const cv::Mat cost = to_f64(cost_in);
        const int h = cost.rows, w = cost.cols;
        const int sy = start ? start->y : 0;
        const int sx = start ? start->x : -1;
        const int gy = goal ? goal->y : h - 1;
        if (sy > gy) throw std::invalid_argument("DP requiert start_y <= goal_y");

        cv::Mat dist, parent;
        dp_fill(cost, sy, sx, gy, dist, parent);

        int ex = 0;
        if (goal) {
            ex = goal->x;
        }
        else {                                           // np.argmin : premier minimum
            const double* row = dist.ptr<double>(gy);
            for (int x = 1; x < w; ++x)
                if (row[x] < row[ex]) ex = x;
        }
        if (!std::isfinite(dist.at<double>(gy, ex))) throw std::runtime_error("Pas de chemin DP contraint");

        PathResult res;
        int x = ex;
        for (int y = gy; y >= sy; --y) {
            res.path.push_back(cv::Point(x, y));
            if (y > sy) x = parent.at<int>(y, x);
        }
        std::reverse(res.path.begin(), res.path.end());
        res.objective = dist.at<double>(gy, ex);
        res.expanded = static_cast<long long>(gy - sy + 1) * w;
        return res;
    }

    cv::Mat dp_distance_top(const cv::Mat& cost_in) {
        const cv::Mat cost = to_f64(cost_in);
        const int h = cost.rows, w = cost.cols;
        cv::Mat d(cost.size(), CV_64F, cv::Scalar(INF));
        cost.row(0).copyTo(d.row(0));
        for (int y = 1; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double best = INF;
                for (int px = x - 1; px <= x + 1; ++px) {
                    if (0 <= px && px < w) {
                        const double val = d.at<double>(y - 1, px) + edge_weight(cost, y - 1, px, y, x);
                        if (val < best) best = val;
                    }
                }
                d.at<double>(y, x) = best;
            }
        }
        return d;
    }

}  // namespace seamlib
