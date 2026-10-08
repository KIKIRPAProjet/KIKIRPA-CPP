#pragma once
#include "algorithms/matching/correlation_phase/Types.h"
#include <utility>
#include <vector>

namespace stitching::matching::correlation_phase {

// Étape 3 : mise en forme du résultat au format commun des FeatureMatcher (Matches).
class Sortie {
public:
    // Translation + nbPoints meilleurs points, dans le repère des tuiles d'origine
    // (orientation demandée par le pipeline rétablie). origineA : coin de la zone commune dans A.
    static Matches construire(const PaireOrientee& paire, const Mesure& mesure, cv::Point origineA,
                              std::vector<Correspondance> points, int nbPoints);

private:
    // Tri par confiance décroissante, puis on garde au plus nbPoints points
    static void trierEtLimiter(std::vector<Correspondance>& points, int nbPoints);

    // (point dans A, point dans B), en coordonnées des tuiles entières
    static std::pair<cv::Point2f, cv::Point2f> versTuiles(const Correspondance& c, cv::Point origineA);

    // A et B avaient été échangées : on inverse la translation et on échange pointsA / pointsB
    static void annulerOrientation(Matches& m);
};

} // namespace stitching::matching::correlation_phase
