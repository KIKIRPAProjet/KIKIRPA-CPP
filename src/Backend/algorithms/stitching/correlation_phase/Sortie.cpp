// Sortie.cpp
#include "algorithms/matching/correlation_phase/Sortie.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace stitching::matching::correlation_phase {

// ======================================== Public ========================================

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

std::string Sortie::echecPaire(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx) {
    if (a.empty() || b.empty()) return std::string("tuile ") + (a.empty() ? "A" : "B") + " vide (image non chargée)";
    if (a.size() != b.size())
        return "tuiles de tailles différentes (A " + std::to_string(a.cols) + " x " + std::to_string(a.rows) +
               ", B " + std::to_string(b.cols) + " x " + std::to_string(b.rows) + ")";
    return "recouvrement nominal " + nombre(ctx.overlap, 2) + " hors de ]0, 1] (fraction de la tuile, ex. 0.2)";
}

std::string Sortie::echecBandes(const PairContext& ctx, const Parametres& p) {
    return "pas assez de contenu commun dans les bandes de recouvrement nominal (" + nombre(ctx.overlap * 100, 0) +
           " %) : moins de " + nombre(p.fractionValideMin * 100, 0) + " % de pixels exploitables (fond noir, "
           "reflets saturés, zone uniforme) ou zone commune trop petite. Vérifier aussi le recouvrement nominal "
           "et la direction";
}

std::string Sortie::echecGlobal(const Diagnostic& d, const PaireOrientee& paire, const Bandes& bandes,
                                const PairContext& ctx, const Parametres& p) {
    // Écart au recouvrement nominal dans le sens du déplacement (il fixe le recouvrement réel) et en travers
    const bool bas = paire.position == Direction::Bottom;
    const double sens = bas ? d.picLibre.y : d.picLibre.x;
    const double travers = bas ? d.picLibre.x : d.picLibre.y;
    const std::string rapportMarge = "rapport pic / second pic " + nombre(d.rapportMarge, 2) + " < seuil " +
                                     nombre(p.seuilRapport, 2);

    if (!d.horsMarge)
        return "pic de corrélation ambigu dans la marge du rail (" + rapportMarge + ") : zone commune peu "
               "texturée ou motif répétitif";

    const std::string ecart = "le meilleur pic est à " + nombre(std::abs(sens), 0) + " px du recouvrement nominal "
                              "dans le sens du déplacement et " + nombre(std::abs(travers), 0) + " px en travers, "
                              "au-delà de la marge du rail (±" + nombre(bandes.marge, 0) + " px)";
    if (d.rapportLibre < p.seuilRapport)
        return "pic de corrélation ambigu (" + rapportMarge + ") ; " + ecart + ", mais il est lui aussi ambigu "
               "(rapport " + nombre(d.rapportLibre, 2) + ") : zone peu texturée, motif répétitif ou recouvrement "
               "nominal très faux";

    // Recouvrement réel estimé : la bande nominale commence à `origine` ; le pic la décale de `sens`
    const double cote = bas ? paire.A.rows : paire.A.cols;
    const double debutBande = bas ? bandes.origine.y : bandes.origine.x;
    const double recouvrementReel = (cote - debutBande - sens) / cote;
    return "translation hors de la marge du rail : " + ecart + " (rapport " + nombre(d.rapportLibre, 2) +
           "). Augmenter la marge (Parametres::marge >= " +
           nombre(std::ceil(std::max(std::abs(sens), std::abs(travers)) + MARGE_SECURITE), 0) + " px) ou donner le recouvrement "
           "réel (environ " + nombre(recouvrementReel * 100, 0) + " % au lieu de " + nombre(ctx.overlap * 100, 0) +
           " %). À vérifier : sur un motif répétitif, ce pic peut être faux";
}

// ======================================== Private ========================================

std::string Sortie::nombre(double valeur, int decimales) {
    char tampon[64];
    std::snprintf(tampon, sizeof tampon, "%.*f", decimales, valeur);
    return tampon;
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
