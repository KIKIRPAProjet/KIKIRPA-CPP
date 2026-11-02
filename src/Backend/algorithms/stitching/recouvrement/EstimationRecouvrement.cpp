// EstimationRecouvrement.cpp
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "algorithms/stitching/correlation_phase/Algorithme.h"
#include "algorithms/stitching/correlation_phase/Preparation.h"
#include "algorithms/stitching/correlation_phase/Sortie.h"
#include <algorithm>
#include <cmath>

namespace stitching::matching {

using correlation_phase::Algorithme;
using correlation_phase::Mesure;
using correlation_phase::PaireOrientee;
using correlation_phase::Preparation;
using correlation_phase::Sortie;

// ======================================== Public ========================================

EstimationRecouvrement::EstimationRecouvrement(const correlation_phase::Parametres& p) : parametres(p) {
    Preparation::verifierParametres(parametres);
    // Le pixel suffit pour découper la zone commune réelle ; le sous-pixel, le plus coûteux, n'est calculé
    // qu'une fois, sur cette zone (deuxième passe)
    parametres.facteurDft = PRECISION_PIXEL;
}

std::optional<RecouvrementReel> EstimationRecouvrement::estimer(const cv::Mat& a, const cv::Mat& b,
                                                                const PairContext& ctx) {
    raison.clear();
    const auto paire = Preparation::orienter(a, b, ctx);
    if (!paire) {
        raison = Sortie::echecPaire(a, b, ctx);
        return std::nullopt;
    }
    const auto bandes = Preparation::bandes(*paire, ctx.overlap, parametres);
    if (!bandes) {
        raison = Sortie::echecBandes(ctx, parametres);
        return std::nullopt;
    }
    const auto mesure = Algorithme::mesurerGlobal(*bandes, parametres);
    if (!mesure) {
        raison = Sortie::echecGlobal(Algorithme::diagnostiquerGlobal(*bandes, parametres), *paire, *bandes, ctx,
                                     parametres);
        return std::nullopt;
    }
    return versTuiles(*paire, *mesure);
}

std::pair<cv::Rect, cv::Rect> EstimationRecouvrement::zonesCommunes(cv::Size a, cv::Size b, cv::Point2d translation) {
    // Arrondi au pixel : les deux rectangles ont alors exactement la même taille, et ce qui reste du décalage
    // (moins d'un demi-pixel, plus l'erreur de la première passe) est mesuré par la méthode d'appariement
    const cv::Point decalage(static_cast<int>(std::lround(translation.x)), static_cast<int>(std::lround(translation.y)));
    const int x0 = std::max(0, decalage.x), x1 = std::min(a.width, decalage.x + b.width);
    const int y0 = std::max(0, decalage.y), y1 = std::min(a.height, decalage.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    const cv::Rect zoneA(x0, y0, x1 - x0, y1 - y0);
    return {zoneA, zoneA - decalage};
}

// ======================================== Private ========================================

RecouvrementReel EstimationRecouvrement::versTuiles(const PaireOrientee& paire, const Mesure& mesure) {
    // Paire orientée : B est en bas ou à droite, son coin est à (tuile − recouvrement) dans le sens du déplacement
    const bool bas = paire.position == Direction::Bottom;
    const double cote = bas ? paire.A.rows : paire.A.cols;
    const double debutB = bas ? mesure.ty : mesure.tx;

    RecouvrementReel r;
    r.translation = Preparation::orienterTranslation(paire, {mesure.tx, mesure.ty});
    r.overlap     = static_cast<float>((cote - debutB) / cote);
    r.rapport     = mesure.rapport;
    return r;
}

} // namespace stitching::matching
