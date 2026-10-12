// Sortie.cpp
#include "algorithms/matching/correlation_phase/Sortie.h"
#include <algorithm>

namespace stitching::matching::correlation_phase {

Matches Sortie::construire(const PaireOrientee& paire, const Mesure& mesure, cv::Point origineA,
                           std::vector<Correspondance> points, int nbPoints) {
    Matches m;
    m.x = static_cast<float>(mesure.tx);
    m.y = static_cast<float>(mesure.ty);

    trierEtLimiter(points, nbPoints);
    m.pointsA.reserve(points.size());
    m.pointsB.reserve(points.size());
    m.confidence.reserve(points.size());
    for (const Correspondance& c : points) {
        const auto [pointA, pointB] = versTuiles(c, origineA);
        m.pointsA.push_back(pointA);
        m.pointsB.push_back(pointB);
        m.confidence.push_back(c.confiance);
    }

    if (paire.inversee) annulerOrientation(m);
    return m;
}

void Sortie::trierEtLimiter(std::vector<Correspondance>& points, int nbPoints) {
    std::sort(points.begin(), points.end(),
              [](const Correspondance& u, const Correspondance& v) { return u.confiance > v.confiance; });
    if (static_cast<int>(points.size()) > nbPoints) points.resize(std::max(0, nbPoints));
}

std::pair<cv::Point2f, cv::Point2f> Sortie::versTuiles(const Correspondance& c, cv::Point origineA) {
    // Un pixel (X, Y) de A correspond au pixel (X, Y) - translation de B
    const cv::Point2f pointA = c.pointA + cv::Point2f(origineA);
    const cv::Point2f pointB = pointA - cv::Point2f(static_cast<float>(c.translation.x),
                                                    static_cast<float>(c.translation.y));
    return {pointA, pointB};
}

void Sortie::annulerOrientation(Matches& m) {
    m.x = -m.x;
    m.y = -m.y;
    std::swap(m.pointsA, m.pointsB);
}

} // namespace stitching::matching::correlation_phase
