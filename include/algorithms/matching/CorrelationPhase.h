#pragma once //copié que une seule fois
#include "algorithms/matching/FeatureMatcher.h"
#include "algorithms/matching/correlation_phase/Types.h"

namespace stitching::matching {

// Corrélation de phase : translation entre deux tuiles voisines, mesurée sur les bandes de recouvrement,
// puis points de correspondance raffinés localement (port de correlation_phase.py).
// Seul header à inclure depuis le pipeline ; le détail est dans correlation_phase/ :
//   Preparation (images de travail) → Algorithme (mesures) → Sortie (Matches).
//
// Paramètres (JSON, tous optionnels ; std::invalid_argument si invalides) :
//   lissage        3.0  sigma (px) du flou gaussien de la surface de corrélation globale
//   seuil_rapport  1.5  rapport minimal pic / second pic pour accepter une mesure
//   nb_points      500  nombre maximal de points renvoyés (0 = translation seule)
//   fenetre        64   côté (px) des patchs de corrélation locale
//   lissage_local  1.5  sigma (px) du flou gaussien des surfaces locales
//   ecart_max      3.0  écart maximal (px) entre la translation locale et la globale
//   espacement_min 16   distance minimale (px) entre deux points
//   marge          0    écart maximal (px) au recouvrement nominal (0 = 5 % de la tuile, au moins 48 px)
//   fraction_valide_min 0.25  part minimale de pixels exploitables communs aux deux bandes
//   facteur_dft    100  précision du sous-pixel : 1/facteur_dft px
class CorrelationPhase : public FeatureMatcher {
public:
    explicit CorrelationPhase(const Json& params = {});

    // false si la translation ne peut pas être mesurée (tuiles vides ou de tailles différentes,
    // pas de contenu commun, pic ambigu). true : out.x, out.y valides, points éventuellement vides.
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;

private:
    correlation_phase::Parametres parametres;   // lus une seule fois, dans le constructeur
};

} // namespace stitching::matching
