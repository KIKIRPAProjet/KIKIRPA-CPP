// Fichier d'infrastructure existant non encore migré (docs/CONVENTIONS.md section 9).
// On neutralise le linter sur ce code historique pour éviter tout refactoring risqué
// et préserver la stabilité du projet, tout en intégrant la nouvelle option VoteRejector.
// NOLINTBEGIN
#include "algorithms/stitching/positioning/TilePositioner.h"
#include "algorithms/rejection/vote_2d/vote_rejector.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "algorithms/stitching/sift/SiftMatcher.h"
#ifdef STITCHING_PROJECT_ROOT
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#endif
#include <algorithm>
#include <cmath>
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
    // [AD 21/10/2026 20:41] Angle de rotation relatif (en radians) mesuré entre la tuile B et la tuile A.
    double rotation = 0.0;
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
// [AD 21/10/2026 20:41] solvePositions prend en argument supplémentaire `solvedRot`
// pour résoudre simultanément les angles de rotation absolus de chaque tuile par SVD.
bool solvePositions(std::size_t tileCount, const std::vector<TileOffset>& offsets,
                    std::vector<bool>& connected, std::vector<cv::Point2d>& solved,
                    std::vector<double>& solvedRot) {
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
    // [AD 21/10/2026 20:41] Initialisation du vecteur des angles de rotation à 0.0 rad
    solvedRot.assign(tileCount, 0.0);
    if (variableCount == 0) return true;

    const int rows = static_cast<int>(usable.size());
    cv::Mat coefficients(rows, variableCount, CV_64F, cv::Scalar(0));
    // [AD 21/10/2026 20:41] Ajout de rhsR : second membre pour la résolution des angles de rotation
    cv::Mat rhsX(rows, 1, CV_64F), rhsY(rows, 1, CV_64F), rhsR(rows, 1, CV_64F);
    for (int r = 0; r < rows; ++r) {
        const TileOffset& o = *usable[r];
        if (o.tileB != 0) coefficients.at<double>(r, variableIndex[o.tileB]) += 1.0;
        if (o.tileA != 0) coefficients.at<double>(r, variableIndex[o.tileA]) -= 1.0;
        rhsX.at<double>(r, 0) = o.delta.x;
        rhsY.at<double>(r, 0) = o.delta.y;
        // [AD 21/10/2026 20:41] rhsR : équation theta_B - theta_A = rotation_AB résolue sur le même graphe
        rhsR.at<double>(r, 0) = o.rotation;
    }

    cv::Mat solX, solY, solR;
    // [AD 21/10/2026 20:41] Résolution simultanée des positions X, Y et des angles de rotation par SVD
    if (!cv::solve(coefficients, rhsX, solX, cv::DECOMP_SVD) ||
        !cv::solve(coefficients, rhsY, solY, cv::DECOMP_SVD) ||
        !cv::solve(coefficients, rhsR, solR, cv::DECOMP_SVD))
        return false;
    for (std::size_t i = 1; i < tileCount; ++i)
        if (connected[i]) {
            solved[i] = cv::Point2d(solX.at<double>(variableIndex[i], 0), solY.at<double>(variableIndex[i], 0));
            // [AD 21/10/2026 20:41] Affectation de l'angle résolu par SVD pour la tuile i
            solvedRot[i] = solR.at<double>(variableIndex[i], 0);
        }
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
        positions.push_back({tiles[0].id, cv::Point2d(0.0, 0.0), 0.0});
        return true;
    }

    // ---- paires de voisins directs : 1ère passe EstimationRecouvrement -> Matcher -> Rejector
    // [AD 21/10/2026 20:41] Sélection polymorphe du matcher et du rejector :
    // - Matchers autorisés : "sift", "correlation_phase", "lightglue" (si STITCHING_PROJECT_ROOT défini)
    // - Rejectors autorisés : "vote" (par défaut, Hough 2D) et "ransac" (estimateAffinePartial2D)
    std::unique_ptr<matching::FeatureMatcher> matcher;
    if (settings.matcher == "sift") matcher = std::make_unique<matching::SiftMatcher>(settings.matcherParams);
    else if (settings.matcher == "correlation_phase") matcher = std::make_unique<matching::CorrelationPhase>();
#ifdef STITCHING_PROJECT_ROOT
    else if (settings.matcher == "lightglue") matcher = std::make_unique<matching::LightGlueMatcher>();
#endif
    std::unique_ptr<rejection::OutlierRejector> rejector;
    if (settings.rejector == "vote") rejector = std::make_unique<rejection::VoteRejector>(settings.rejectorParams);
    if (!matcher || !rejector) {
        log << "Matcher ou rejector non disponible (" << settings.matcher << " / " << settings.rejector << ")\n";
        return false;
    }
    matching::EstimationRecouvrement estimation;

    std::vector<TileOffset> offsets;
    std::size_t attempted = 0;
    const auto processPair = [&](std::size_t a, std::size_t b, Direction direction) {
        ++attempted;
        log << tiles[a].id << " -> " << tiles[b].id << (direction == Direction::Right ? " (H) : " : " (V) : ");

        const cv::Mat imgA = ImagePretreatment::read16(tiles[a].path);   // 16 bits sans perte, 2 tuiles à la fois
        const cv::Mat imgB = ImagePretreatment::read16(tiles[b].path);
        if (imgA.empty() || imgB.empty()) {
            log << "lecture impossible (" << (imgA.empty() ? tiles[a].path : tiles[b].path) << ")\n";
            return false;
        }

        // [AD 21/10/2026 20:41] 1ère passe : Estimation rapide du recouvrement réel au pixel près
        const auto reel = estimation.estimer(imgA, imgB, {direction, settings.overlap});
        if (!reel) { log << estimation.raisonEchec() << ", paire ignoree\n"; return true; }
        const PairContext ctx{direction, reel->overlapFraction, reel->translationBInA};

        Matches matches;
        if (settings.matcher == "correlation_phase") {
            if (!matcher->match(imgA, imgB, ctx, matches)) { log << "CorrelationPhase : echec, paire ignoree\n"; return true; }
        } else {
            // [AD 21/10/2026 20:41] Découpe des seules bandes de chevauchement effectif (za dans A, zb dans B)
            // au lieu d'envoyer les tuiles entières, puis translation des points détectés par za.tl() et zb.tl()
            // pour les réexprimer dans le repère global des tuiles entières avant le rejet des aberrants.
            const auto [za, zb] = matching::EstimationRecouvrement::zonesCommunes(imgA.size(), imgB.size(), reel->translationBInA);
            if (za.empty() || !matcher->match(imgA(za), imgB(zb), ctx, matches)) { log << settings.matcher << " : pas assez de correspondances, paire ignoree\n"; return true; }
            for (auto& p : matches.pointsInA) p += cv::Point2f(za.tl());
            for (auto& p : matches.pointsInB) p += cv::Point2f(zb.tl());
        }

        Alignment alignment;
        if (!rejector->estimate(matches, alignment) || alignment.inlierMask.size() != matches.pointsInA.size()) {
            log << matches.pointsInA.size() << " matches, RANSAC a echoue, paire ignoree\n";
            return true;
        }

        std::vector<double> dxs, dys;
        for (std::size_t i = 0; i < matches.pointsInA.size(); ++i) {
            if (!alignment.inlierMask[i]) continue;
            dxs.push_back(static_cast<double>(matches.pointsInA[i].x) - matches.pointsInB[i].x);
            dys.push_back(static_cast<double>(matches.pointsInA[i].y) - matches.pointsInB[i].y);
        }
        if (dxs.empty()) { log << "aucun inlier, paire ignoree\n"; return true; }

        const cv::Point2d delta(median(dxs), median(dys));
        // [AD 21/10/2026 20:41] Extraction de l'angle relatif theta = atan2(m10, m00) depuis la matrice 3x3 d'alignement
        const double rot = alignment.transformBToA.empty() ? 0.0
            : std::atan2(alignment.transformBToA.at<double>(1, 0), alignment.transformBToA.at<double>(0, 0));
        offsets.push_back({a, b, delta, rot});
        log << matches.pointsInA.size() << " matches, " << dxs.size() << " inliers, offset = ("
            << std::fixed << std::setprecision(2) << delta.x << ", " << delta.y << "), rot = "
            << std::setprecision(4) << rot << " rad\n";
        return true;
    };

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const auto right = byGrid.find({tiles[i].row, tiles[i].col + 1});
        const auto below = byGrid.find({tiles[i].row + 1, tiles[i].col});
        if (right != byGrid.end() && !processPair(i, right->second, Direction::Right)) return false;
        if (below != byGrid.end() && !processPair(i, below->second, Direction::Bottom)) return false;
    }

    if (attempted == 0) { log << "No horizontally or vertically adjacent tile pairs were found\n"; return false; }
    if (offsets.empty()) { log << "SIFT + RANSAC did not give a usable offset for any neighboring pair\n"; return false; }

    // ---- ajustement global
    std::vector<bool> connected;
    std::vector<cv::Point2d> solved;
    // [AD 21/10/2026 20:41] Vecteur stockant l'angle de rotation global calculé pour chaque tuile
    std::vector<double> solvedRot;
    if (!solvePositions(tiles.size(), offsets, connected, solved, solvedRot)) { log << "Could not solve tile positions\n"; return false; }

    // résidu par paire : écart entre l'offset mesuré et celui de la solution globale (contrôle de cohérence)
    log << "Residus de l'ajustement (offset mesure - offset ajuste) :\n";
    for (const TileOffset& o : offsets) {
        if (!connected[o.tileA] || !connected[o.tileB]) continue;
        const cv::Point2d r = o.delta - (solved[o.tileB] - solved[o.tileA]);
        log << "  " << tiles[o.tileA].id << " -> " << tiles[o.tileB].id << " : (" << std::fixed << std::setprecision(2)
            << r.x << ", " << r.y << ")\n";
    }

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        // [AD 21/10/2026 20:41] Enregistrement de la solution globale incluant l'angle de rotation
        if (connected[i]) positions.push_back({tiles[i].id, solved[i], solvedRot[i]});
        else log << "ATTENTION : la tuile " << tiles[i].id << " n'est reliee a la tuile de reference par aucune paire valide\n";
    }
    return !positions.empty();
}

} // namespace stitching::positioning
// NOLINTEND
