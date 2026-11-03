// CorrelationPhase.cpp
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "algorithms/stitching/correlation_phase/Algorithme.h"
#include "algorithms/stitching/correlation_phase/Preparation.h"
#include "algorithms/stitching/correlation_phase/Sortie.h"

namespace stitching::matching {

using correlation_phase::Algorithme;
using correlation_phase::Correspondance;
using correlation_phase::Preparation;
using correlation_phase::Sortie;

CorrelationPhase::CorrelationPhase(const Parametres& p) : parametres(p), premierePasse(p) {
    Preparation::verifierParametres(parametres);
}

bool CorrelationPhase::match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) {
    out = {};
    raison.clear();

    // 1. Première passe : position de B au pixel près sur les bandes nominales, sauf si le pipeline l'a faite
    std::optional<cv::Point2d> translation = ctx.translation;
    if (!translation) {
        const auto reel = premierePasse.estimer(a, b, ctx);
        if (!reel) {
            raison = premierePasse.raisonEchec();
            return false;
        }
        translation = reel->translation;
    }

    // 2. Deuxième passe : translation au sous-pixel sur la zone commune réelle (en cas d'échec : message dans `raison`)
    const auto paire = Preparation::orienter(a, b, ctx);
    if (!paire) {
        raison = Sortie::echecPaire(a, b, ctx);
        return false;
    }
    const auto zone = Preparation::zoneCommune(*paire, Preparation::orienterTranslation(*paire, *translation), parametres);
    if (!zone) {
        raison = Sortie::echecZoneCommune(*translation, parametres);
        return false;
    }
    const auto mesure = Algorithme::affinerGlobal(*zone, parametres);
    if (!mesure) {
        raison = Sortie::echecAffinage(*translation, *zone);
        return false;
    }

    // 3. Points raffinés localement dans la zone commune réelle (absents si elle est trop étroite)
    const auto recouvrement = Preparation::recouvrement(*paire, *mesure, parametres);
    std::vector<Correspondance> points;
    if (recouvrement && parametres.nbPoints > 0) points = Algorithme::raffiner(*recouvrement, *mesure, parametres);

    // 4. Mise en forme dans le repère des tuiles d'origine
    out = Sortie::construire(*paire, *mesure, recouvrement ? recouvrement->origineA : cv::Point(),
                             std::move(points), parametres.nbPoints);
    return true;
}

} // namespace stitching::matching
