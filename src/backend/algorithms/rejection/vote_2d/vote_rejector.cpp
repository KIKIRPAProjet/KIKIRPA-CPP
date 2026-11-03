// src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp
// NOLINTBEGIN(misc-include-cleaner)
#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "algorithms/legacy_type_aliases.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace stitching::rejection { // NOLINT(readability-identifier-naming)

namespace {

// Planchers stricts de sécurité pour éviter les valeurs négatives ou une division par zéro.
constexpr double MINIMUM_REPROJECTION_THRESHOLD_PX = 1e-6;
constexpr double MINIMUM_GRID_CELL_SIZE_PX = 1e-6;

// Valeurs par défaut des paramètres de vote et de seuil.
constexpr double DEFAULT_REPROJECTION_THRESHOLD_PX = 1.5;
constexpr double DEFAULT_GRID_CELL_SIZE_PX = 1.0;
constexpr int DEFAULT_MIN_MATCHES_COUNT = 3;
constexpr int DEFAULT_MIN_INLIERS_COUNT = 3;
constexpr int MINIMUM_GEOMETRIC_POINTS_COUNT = 2;

// Constantes géométriques et algorithmiques.
constexpr double GRID_CELL_CENTER_OFFSET = 0.5;
constexpr double EVEN_MEDIAN_AVERAGE_FACTOR = 0.5;
constexpr int NEIGHBORHOOD_WINDOW_RADIUS = 1;
constexpr int MEDIAN_REFINEMENT_ITERATIONS = 3;
constexpr int HOMOGRAPHY_MATRIX_SIZE = 3;

struct ValidDeltas {
    std::vector<double> deltaX;
    std::vector<double> deltaY;
    std::vector<std::size_t> validIndices;
};

// Filtre les coordonnées finies (élimine NaN et Inf) et calcule les déplacements A - B.
ValidDeltas extractFiniteDeltas(const Matches& matches) {
    ValidDeltas deltas;
    deltas.deltaX.reserve(matches.pointsInA.size());
    deltas.deltaY.reserve(matches.pointsInA.size());
    deltas.validIndices.reserve(matches.pointsInA.size());
    for (std::size_t i = 0; i < matches.pointsInA.size(); ++i) {
        const auto& pointA = matches.pointsInA[i];
        const auto& pointB = matches.pointsInB[i];
        if (!std::isfinite(pointA.x) || !std::isfinite(pointA.y) ||
            !std::isfinite(pointB.x) || !std::isfinite(pointB.y)) {
            continue;
        }
        deltas.deltaX.push_back(static_cast<double>(pointA.x) - static_cast<double>(pointB.x));
        deltas.deltaY.push_back(static_cast<double>(pointA.y) - static_cast<double>(pointB.y));
        deltas.validIndices.push_back(i);
    }
    return deltas;
}

// Ajoute un vote dans le voisinage 3x3 autour de la cellule (cellY, cellX).
void accumulateNeighborhoodVotes(const std::pair<int, int>& centerCell,
                                 std::map<std::pair<int, int>, int>& votes) {
    for (int offsetY = -NEIGHBORHOOD_WINDOW_RADIUS; offsetY <= NEIGHBORHOOD_WINDOW_RADIUS; ++offsetY) {
        for (int offsetX = -NEIGHBORHOOD_WINDOW_RADIUS; offsetX <= NEIGHBORHOOD_WINDOW_RADIUS; ++offsetX) {
            ++votes[{centerCell.first + offsetY, centerCell.second + offsetX}];
        }
    }
}

// Détermine le mode de translation via accumulation 2D lissée sur grille (voisinage 3x3).
std::pair<double, double> computeVoteMode(const ValidDeltas& deltas, double cellSizePx) {
    std::map<std::pair<int, int>, int> votes;
    for (std::size_t i = 0; i < deltas.validIndices.size(); ++i) {
        int cellX = static_cast<int>(std::floor(deltas.deltaX[i] / cellSizePx));
        int cellY = static_cast<int>(std::floor(deltas.deltaY[i] / cellSizePx));
        accumulateNeighborhoodVotes({cellY, cellX}, votes);
    }
    if (votes.empty()) {
        return {0.0, 0.0};
    }
    auto bestCell = votes.begin();
    for (auto it = votes.begin(); it != votes.end(); ++it) {
        if (it->second > bestCell->second) {
            bestCell = it;
        }
    }
    double modeX = (static_cast<double>(bestCell->first.second) + GRID_CELL_CENTER_OFFSET) * cellSizePx;
    double modeY = (static_cast<double>(bestCell->first.first) + GRID_CELL_CENTER_OFFSET) * cellSizePx;
    return {modeX, modeY};
}

// Calcule la médiane robuste L1 d'une série de valeurs scalaires.
double computeMedian(std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    double medianValue = *middle;
    if (values.size() % 2 == 0) {
        double maxLower = *std::max_element(values.begin(), middle);
        medianValue = EVEN_MEDIAN_AVERAGE_FACTOR * (medianValue + maxLower);
    }
    return medianValue;
}

// Extrait les inliers pour une passe de raffinement de translation.
std::pair<std::vector<double>, std::vector<double>> collectInliersForCenter(
    const ValidDeltas& deltas, const std::pair<double, double>& center, double thresholdPx) {
    std::vector<double> inliersX;
    std::vector<double> inliersY;
    for (std::size_t i = 0; i < deltas.validIndices.size(); ++i) {
        if (std::hypot(deltas.deltaX[i] - center.first, deltas.deltaY[i] - center.second) <= thresholdPx) {
            inliersX.push_back(deltas.deltaX[i]);
            inliersY.push_back(deltas.deltaY[i]);
        }
    }
    return {inliersX, inliersY};
}

// Raffine l'estimation de translation par médiane itérative L1 sur les inliers.
std::pair<double, double> refineTranslation(const ValidDeltas& deltas,
                                            const std::pair<double, double>& initialMode,
                                            double thresholdPx) {
    double currentX = initialMode.first;
    double currentY = initialMode.second;
    for (int iter = 0; iter < MEDIAN_REFINEMENT_ITERATIONS; ++iter) {
        auto inliers = collectInliersForCenter(deltas, {currentX, currentY}, thresholdPx);
        if (inliers.first.empty()) {
            break;
        }
        currentX = computeMedian(inliers.first);
        currentY = computeMedian(inliers.second);
    }
    return {currentX, currentY};
}

// Marque les inliers finaux dans le masque binaire et renvoie leur nombre total.
int markInliers(const ValidDeltas& deltas,
                const std::pair<double, double>& translation,
                double thresholdPx,
                std::vector<unsigned char>& inlierMask) {
    int inlierCount = 0;
    for (std::size_t i = 0; i < deltas.validIndices.size(); ++i) {
        if (std::hypot(deltas.deltaX[i] - translation.first, deltas.deltaY[i] - translation.second) <= thresholdPx) {
            inlierMask[deltas.validIndices[i]] = 1;
            ++inlierCount;
        }
    }
    return inlierCount;
}

} // namespace

VoteRejectorClass::VoteRejectorClass(const Json& params)
    : parameters(params.is_object() ? params : Json::object()) {}

bool VoteRejectorClass::estimate(const Matches& matches, Alignment& out) {
    out.transformBToA.release();
    out.inlierMask.clear();
    if (matches.pointsInA.size() != matches.pointsInB.size()) return false;

    int minMatches = std::max(MINIMUM_GEOMETRIC_POINTS_COUNT, parameters.value("minMatches", DEFAULT_MIN_MATCHES_COUNT));
    int minInliers = std::max(MINIMUM_GEOMETRIC_POINTS_COUNT, parameters.value("minInliers", DEFAULT_MIN_INLIERS_COUNT));
    double thresholdPx = std::max(MINIMUM_REPROJECTION_THRESHOLD_PX,
        parameters.value("seuil", parameters.value("ransacReprojThreshold", DEFAULT_REPROJECTION_THRESHOLD_PX)));
    double cellSizePx = std::max(MINIMUM_GRID_CELL_SIZE_PX, parameters.value("case", DEFAULT_GRID_CELL_SIZE_PX));

    ValidDeltas deltas = extractFiniteDeltas(matches);
    if (deltas.validIndices.size() < static_cast<std::size_t>(minMatches)) return false;

    std::pair<double, double> mode = computeVoteMode(deltas, cellSizePx);
    std::pair<double, double> translation = refineTranslation(deltas, mode, thresholdPx);

    out.inlierMask.assign(matches.pointsInA.size(), 0);
    int inlierCount = markInliers(deltas, translation, thresholdPx, out.inlierMask);
    if (inlierCount < minInliers) {
        out.inlierMask.assign(matches.pointsInA.size(), 0);
        return false;
    }
    out.transformBToA = cv::Mat::eye(HOMOGRAPHY_MATRIX_SIZE, HOMOGRAPHY_MATRIX_SIZE, CV_64F);
    out.transformBToA.at<double>(0, 2) = translation.first;
    out.transformBToA.at<double>(1, 2) = translation.second;
    return true;
}

} // namespace stitching::rejection
// NOLINTEND(misc-include-cleaner)

