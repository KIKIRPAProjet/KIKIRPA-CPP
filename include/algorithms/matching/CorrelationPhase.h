#pragma once
#include "algorithms/matching/FeatureMatcher.h"

namespace stitching::matching {

// Corrélation de phase : translation entre deux tuiles voisines, mesurée sur les bandes de recouvrement,
// puis points de correspondance raffinés localement (port de correlation_phase.py).
//
// Paramètres (JSON, tous optionnels) :
//   lissage        3.0  sigma (px) du flou gaussien de la surface de corrélation globale
//   seuil_rapport  1.5  rapport minimal pic / second pic pour accepter une mesure
//   nb_points      500  nombre maximal de points renvoyés
//   fenetre        64   côté (px) des patchs de corrélation locale
//   lissage_local  1.5  sigma (px) du flou gaussien des surfaces locales
//   ecart_max      3.0  écart maximal (px) entre la translation locale et la globale
//   espacement_min 16   distance minimale (px) entre deux points
class CorrelationPhase : public FeatureMatcher {
    Json params;
public:
    explicit CorrelationPhase(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};

} // namespace stitching::matching
