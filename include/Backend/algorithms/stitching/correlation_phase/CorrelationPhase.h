#pragma once //copié que une seule fois
#include "algorithms/stitching/FeatureMatcher.h"
#include "algorithms/stitching/correlation_phase/Types.h"
#include <string>

namespace stitching::matching {

// Corrélation de phase : translation entre deux tuiles voisines, mesurée sur les bandes de recouvrement,
// puis points de correspondance raffinés localement (port de correlation_phase.py).
// Seul header à inclure depuis le pipeline ; le détail est dans correlation_phase/ :
//   Preparation (images de travail) → Algorithme (mesures) → Sortie (Matches).
//
// Paramètres : structure CorrelationPhase::Parametres (correlation_phase/Types.h), déjà remplie par l'appelant.
// Chaque champ a une valeur par défaut ; std::invalid_argument à la construction si une valeur est invalide.
//   lissage        3.0  sigma (px) du flou gaussien de la surface de corrélation globale
//   seuilRapport   1.5  rapport minimal pic / second pic pour accepter une mesure
//   nbPoints       10   nombre maximal de points renvoyés (0 = translation seule)
//   fenetre        64   côté (px) des patchs de corrélation locale
//   lissageLocal   1.5  sigma (px) du flou gaussien des surfaces locales
//   ecartMax       3.0  écart maximal (px) entre la translation locale et la globale
//   espacementMin  16   distance minimale (px) entre deux points
//   marge          0    écart maximal (px) au recouvrement nominal (0 = 5 % de la tuile, au moins 48 px)
//   fractionValideMin 0.25  part minimale de pixels exploitables communs aux deux bandes
//   facteurDft     100  précision du sous-pixel : 1/facteurDft px
class CorrelationPhase : public FeatureMatcher {
public:
    using Parametres = correlation_phase::Parametres;

    // Sans argument : toutes les valeurs par défaut
    explicit CorrelationPhase(const Parametres& p = {});

    // false si la translation ne peut pas être mesurée (tuiles vides ou de tailles différentes,
    // pas de contenu commun, pic hors de la marge du rail ou ambigu) : la raison est dans raisonEchec().
    // true : out.x, out.y valides, points éventuellement vides.
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
//override car remplce une fonction virtuelle de la classe mere

    // Pourquoi le dernier match() a renvoyé false, avec la correction à apporter quand elle est connue
    // (ex. « Augmenter la marge (Parametres::marge >= 520 px) »). Vide si le dernier match() a réussi.
    const std::string& raisonEchec() const { return raison; }

private:
    Parametres parametres;   // vérifiés une seule fois, dans le constructeur
    std::string raison;      // message du dernier échec de match()
};

} // namespace stitching::matching
