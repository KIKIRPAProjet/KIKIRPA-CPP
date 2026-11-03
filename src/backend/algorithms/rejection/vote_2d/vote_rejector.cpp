// src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp
#include "algorithms/rejection/vote_2d/vote_rejector.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace stitching::rejection {

VoteRejector::VoteRejector(const Json& params) : parameters(params.is_object() ? params : Json::object()) {}

bool VoteRejector::estimate(const Matches& matches, Alignment& out) {
    out.transformBToA.release();
    out.inlierMask.clear();
    if (matches.pointsInA.size() != matches.pointsInB.size()) return false;

    const int minMatches = std::max(2, parameters.value("minMatches", 3));
    const int minInliers = std::max(2, parameters.value("minInliers", 3));
    const double thresholdPx = std::max(1e-6, parameters.value("seuil", parameters.value("ransacReprojThreshold", 1.5)));
    const double cellSizePx = std::max(1e-6, parameters.value("case", 1.0));

    // 1. Filtrage des coordonnées finies (élimination NaN/Inf) et calcul des déplacements (A - B)
    std::vector<double> deltaX, deltaY;
    std::vector<std::size_t> validIndices;
    for (std::size_t i = 0; i < matches.pointsInA.size(); ++i) {
        const auto& pointA = matches.pointsInA[i];
        const auto& pointB = matches.pointsInB[i];
        if (!std::isfinite(pointA.x) || !std::isfinite(pointA.y) || !std::isfinite(pointB.x) || !std::isfinite(pointB.y)) continue;
        deltaX.push_back(static_cast<double>(pointA.x) - pointB.x);
        deltaY.push_back(static_cast<double>(pointA.y) - pointB.y);
        validIndices.push_back(i);
    }
    const std::size_t validCount = validIndices.size();
    if (validCount < static_cast<std::size_t>(minMatches)) return false;

    // 2. Accumulation 2D avec lissage par voisinage 3x3
    std::map<std::pair<int, int>, int> votes;
    for (std::size_t i = 0; i < validCount; ++i) {
        const int cellX = static_cast<int>(std::floor(deltaX[i] / cellSizePx));
        const int cellY = static_cast<int>(std::floor(deltaY[i] / cellSizePx));
        for (int offsetY = -1; offsetY <= 1; ++offsetY)
            for (int offsetX = -1; offsetX <= 1; ++offsetX)
                ++votes[{cellY + offsetY, cellX + offsetX}];
    }

    // 3. Extraction déterministe du mode (cellule ayant reçu le maximum de votes)
    auto bestCell = votes.begin();
    for (auto it = votes.begin(); it != votes.end(); ++it)
        if (it->second > bestCell->second) bestCell = it;

    double translationX = (bestCell->first.second + 0.5) * cellSizePx;
    double translationY = (bestCell->first.first + 0.5) * cellSizePx;
    auto computeMedian = [](std::vector<double>& values) {
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        double medianValue = *middle;
        if (values.size() % 2 == 0) medianValue = 0.5 * (medianValue + *std::max_element(values.begin(), middle));
        return medianValue;
    };

    // 4. Raffinement par médiane itérative (3 passes) sur les correspondances dans le rayon thresholdPx
    for (int iter = 0; iter < 3; ++iter) {
        std::vector<double> inliersX, inliersY;
        for (std::size_t i = 0; i < validCount; ++i)
            if (std::hypot(deltaX[i] - translationX, deltaY[i] - translationY) <= thresholdPx) {
                inliersX.push_back(deltaX[i]);
                inliersY.push_back(deltaY[i]);
            }
        if (inliersX.empty()) break;
        translationX = computeMedian(inliersX);
        translationY = computeMedian(inliersY);
    }

    // 5. Marquage final des inliers dans le repère des Matches d'origine
    out.inlierMask.assign(matches.pointsInA.size(), 0);
    int inlierCount = 0;
    for (std::size_t i = 0; i < validCount; ++i) {
        if (std::hypot(deltaX[i] - translationX, deltaY[i] - translationY) <= thresholdPx) {
            out.inlierMask[validIndices[i]] = 1;
            ++inlierCount;
        }
    }
    if (inlierCount < minInliers) { out.inlierMask.assign(matches.pointsInA.size(), 0); return false; }

    // 6. Matrice homogène de pure translation 3x3 (B -> A)
    out.transformBToA = cv::Mat::eye(3, 3, CV_64F);
    out.transformBToA.at<double>(0, 2) = translationX;
    out.transformBToA.at<double>(1, 2) = translationY;
    return true;
}

} // namespace stitching::rejection
