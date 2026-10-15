#include "algorithms/stitching/positioning/TilePositioner.h"
#include "algorithms/rejection/ransac/RansacRejector.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include "algorithms/stitching/sift/SiftMatcher.h"
#include <algorithm>
#include <iomanip>
#include <map>
#include <memory>
#include <opencv2/core.hpp>
#include <ostream>

namespace stitching::positioning {
namespace {

struct TileInfo {
    std::string path;
    std::string id;
    int row = 0, col = 0;
};

// Décalage mesuré entre deux tuiles voisines : positionB - positionA
struct TileOffset {
    std::size_t tileA, tileB;
    cv::Point2d delta;
};

double median(std::vector<double> values) {
    const std::size_t n = values.size();
    const auto middle = values.begin() + n / 2;
    std::nth_element(values.begin(), middle, values.end());
    double m = *middle;
    if (n % 2 == 0) m = 0.5 * (m + *std::max_element(values.begin(), middle));
    return m;
}

// Moindres carrés : positionB - positionA = delta pour toutes les paires. Tuile 0 = référence (0, 0).
// Seules les tuiles reliées à la référence sont résolues (connected[i] = true).
bool solvePositions(std::size_t tileCount, const std::vector<TileOffset>& offsets,
                    std::vector<bool>& connected, std::vector<cv::Point2d>& solved) {
    connected.assign(tileCount, false);
    connected[0] = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (const TileOffset& o : offsets) {
            if (connected[o.tileA] && !connected[o.tileB]) { connected[o.tileB] = true; changed = true; }
            else if (connected[o.tileB] && !connected[o.tileA]) { connected[o.tileA] = true; changed = true; }
        }
    }

    std::vector<int> variableIndex(tileCount, -1);
    int variableCount = 0;
    for (std::size_t i = 1; i < tileCount; ++i)
        if (connected[i]) variableIndex[i] = variableCount++;

    std::vector<const TileOffset*> usable;
    for (const TileOffset& o : offsets)
        if (connected[o.tileA] && connected[o.tileB]) usable.push_back(&o);

    solved.assign(tileCount, cv::Point2d(0.0, 0.0));
    if (variableCount == 0) return true;

    const int rows = static_cast<int>(usable.size());
    cv::Mat coefficients(rows, variableCount, CV_64F, cv::Scalar(0));
    cv::Mat rhsX(rows, 1, CV_64F), rhsY(rows, 1, CV_64F);
    for (int r = 0; r < rows; ++r) {
        const TileOffset& o = *usable[r];
        if (o.tileB != 0) coefficients.at<double>(r, variableIndex[o.tileB]) += 1.0;
        if (o.tileA != 0) coefficients.at<double>(r, variableIndex[o.tileA]) -= 1.0;
        rhsX.at<double>(r, 0) = o.delta.x;
        rhsY.at<double>(r, 0) = o.delta.y;
    }

    cv::Mat solX, solY;
    if (!cv::solve(coefficients, rhsX, solX, cv::DECOMP_SVD) || !cv::solve(coefficients, rhsY, solY, cv::DECOMP_SVD))
        return false;
    for (std::size_t i = 1; i < tileCount; ++i)
        if (connected[i]) solved[i] = cv::Point2d(solX.at<double>(variableIndex[i], 0), solY.at<double>(variableIndex[i], 0));
    return true;
}

} // namespace

bool computeTilePositions(const std::vector<std::string>& tile_paths,
                          const PositioningSettings& settings,
                          std::vector<TilePositionData>& positions,
                          std::ostream& log) {
    positions.clear();
    using Seamline::ImagePretreatment;

    // ---- tuiles : nom -> (ligne, colonne, identifiant)
    std::vector<TileInfo> tiles;
    for (const std::string& path : tile_paths) {
        TileInfo t;
        t.path = path;
        if (!ImagePretreatment::parseTileName(path, t.row, t.col, t.id)) {
            log << "wrong tile name: " << path << '\n';
            return false;
        }
        tiles.push_back(std::move(t));
    }
    if (tiles.empty()) { log << "No tiles\n"; return false; }

    std::sort(tiles.begin(), tiles.end(), [](const TileInfo& l, const TileInfo& r) {
        return l.row != r.row ? l.row < r.row : l.col < r.col;
    });
    std::map<std::pair<int, int>, std::size_t> byGrid;
    for (std::size_t i = 0; i < tiles.size(); ++i)
        if (!byGrid.emplace(std::make_pair(tiles[i].row, tiles[i].col), i).second) {
            log << "Duplicate tile grid position " << tiles[i].id << '\n';
            return false;
        }

    if (tiles.size() == 1) {
        positions.push_back({tiles[0].id, cv::Point2d(0.0, 0.0)});
        return true;
    }

    // ---- paires de voisins directs : SIFT -> RANSAC -> translation sur les inliers
    // (l'ancienne Factory n'existe plus : seuls SIFT et RANSAC sont disponibles ici)
    if (settings.matcher != "sift" || settings.rejector != "ransac") {
        log << "Seuls matcher = \"sift\" et rejector = \"ransac\" sont disponibles\n";
        return false;
    }
    auto matcher = std::make_unique<matching::SiftMatcher>(settings.matcherParams);
    auto rejector = std::make_unique<rejection::RansacRejector>(settings.rejectorParams);

    std::vector<TileOffset> offsets;
    std::size_t attempted = 0;
    const auto processPair = [&](std::size_t a, std::size_t b, Direction direction) {
        ++attempted;
        log << tiles[a].id << " -> " << tiles[b].id << (direction == Direction::LeftRight ? " (H) : " : " (V) : ");

        const cv::Mat imgA = ImagePretreatment::read16(tiles[a].path);   // 16 bits sans perte, 2 tuiles à la fois
        const cv::Mat imgB = ImagePretreatment::read16(tiles[b].path);
        if (imgA.empty() || imgB.empty()) {
            log << "lecture impossible (" << (imgA.empty() ? tiles[a].path : tiles[b].path) << ")\n";
            return false;
        }

        const PairContext ctx{direction, settings.overlap};
        Matches matches;
        if (!matcher->match(imgA, imgB, ctx, matches)) { log << "SIFT : pas assez de correspondances, paire ignoree\n"; return true; }

        Alignment alignment;
        if (!rejector->estimate(matches, alignment) || alignment.inliers.size() != matches.pointsA.size()) {
            log << matches.pointsA.size() << " matches, RANSAC a echoue, paire ignoree\n";
            return true;
        }

        std::vector<double> dxs, dys;
        for (std::size_t i = 0; i < matches.pointsA.size(); ++i) {
            if (!alignment.inliers[i]) continue;
            dxs.push_back(static_cast<double>(matches.pointsA[i].x) - matches.pointsB[i].x);
            dys.push_back(static_cast<double>(matches.pointsA[i].y) - matches.pointsB[i].y);
        }
        if (dxs.empty()) { log << "aucun inlier, paire ignoree\n"; return true; }

        const cv::Point2d delta(median(dxs), median(dys));
        offsets.push_back({a, b, delta});
        log << matches.pointsA.size() << " matches, " << dxs.size() << " inliers, offset = ("
            << std::fixed << std::setprecision(2) << delta.x << ", " << delta.y << ")\n";
        return true;
    };

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const auto right = byGrid.find({tiles[i].row, tiles[i].col + 1});
        const auto below = byGrid.find({tiles[i].row + 1, tiles[i].col});
        if (right != byGrid.end() && !processPair(i, right->second, Direction::LeftRight)) return false;
        if (below != byGrid.end() && !processPair(i, below->second, Direction::BottomRight)) return false;
    }

    if (attempted == 0) { log << "No horizontally or vertically adjacent tile pairs were found\n"; return false; }
    if (offsets.empty()) { log << "SIFT + RANSAC did not give a usable offset for any neighboring pair\n"; return false; }

    // ---- ajustement global
    std::vector<bool> connected;
    std::vector<cv::Point2d> solved;
    if (!solvePositions(tiles.size(), offsets, connected, solved)) { log << "Could not solve tile positions\n"; return false; }

    // résidu par paire : écart entre l'offset mesuré et celui de la solution globale (contrôle de cohérence)
    log << "Residus de l'ajustement (offset mesure - offset ajuste) :\n";
    for (const TileOffset& o : offsets) {
        if (!connected[o.tileA] || !connected[o.tileB]) continue;
        const cv::Point2d r = o.delta - (solved[o.tileB] - solved[o.tileA]);
        log << "  " << tiles[o.tileA].id << " -> " << tiles[o.tileB].id << " : (" << std::fixed << std::setprecision(2)
            << r.x << ", " << r.y << ")\n";
    }

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (connected[i]) positions.push_back({tiles[i].id, solved[i]});
        else log << "ATTENTION : la tuile " << tiles[i].id << " n'est reliee a la tuile de reference par aucune paire valide\n";
    }
    return !positions.empty();
}

} // namespace stitching::positioning
