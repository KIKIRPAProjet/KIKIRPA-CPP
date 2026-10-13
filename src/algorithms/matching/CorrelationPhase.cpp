// CorrelationPhase.cpp
#include "algorithms/matching/CorrelationPhase.h"
#include "algorithms/matching/correlation_phase/Algorithme.h"
#include "algorithms/matching/correlation_phase/Preparation.h"
#include "algorithms/matching/correlation_phase/Sortie.h"

namespace stitching::matching {

using correlation_phase::Algorithme;
using correlation_phase::Correspondance;
using correlation_phase::Preparation;
using correlation_phase::Sortie;

CorrelationPhase::CorrelationPhase(const Parametres& p) : parametres(p) {
    Preparation::verifierParametres(parametres);
}

bool CorrelationPhase::match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) {
    out = {};

    // 1. Translation globale sur les bandes de recouvrement
    const auto paire = Preparation::orienter(a, b, ctx);
    if (!paire) return false;
    const auto bandes = Preparation::bandes(*paire, ctx.overlap, parametres);
    if (!bandes) return false;
    const auto mesure = Algorithme::mesurerGlobal(*bandes, parametres);
    if (!mesure) return false;

    // 2. Points raffinés localement dans la zone commune réelle (absents si elle est trop étroite)
    const auto recouvrement = Preparation::recouvrement(*paire, *mesure, parametres);
    std::vector<Correspondance> points;
    if (recouvrement && parametres.nbPoints > 0) points = Algorithme::raffiner(*recouvrement, *mesure, parametres);

    // 3. Mise en forme dans le repère des tuiles d'origine
    out = Sortie::construire(*paire, *mesure, recouvrement ? recouvrement->origineA : cv::Point(),
                             std::move(points), parametres.nbPoints);
    return true;
}

} // namespace stitching::matching
