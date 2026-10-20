#pragma once
#include "algorithms/stitching/correlation_phase/Types.h"
#include <optional>
#include <string>
#include <utility>

namespace stitching::matching {

// Première passe, commune à toutes les méthodes d'appariement : position de B par rapport à A au pixel près,
// mesurée par corrélation de phase sur les bandes de recouvrement nominal (ctx.overlap), et recouvrement réel
// qui s'en déduit. Les méthodes d'appariement travaillent ensuite sur la zone commune réelle (zonesCommunes) :
// la corrélation de phase y mesure la translation au sous-pixel (deuxième passe), SIFT et LightGlue y cherchent
// leurs points.
//
// Paramètres : ceux de la corrélation de phase (correlation_phase::Parametres). Seuls lissage, seuilRapport,
// marge et fractionValideMin servent ; facteurDft est ignoré (mesure au pixel près).
// std::invalid_argument à la construction si une valeur est invalide.
class EstimationRecouvrement {
public:
    explicit EstimationRecouvrement(const correlation_phase::Parametres& p = {});

    // Position de B dans A au pixel près et recouvrement réel. Vide si la mesure est impossible ou peu fiable
    // (mêmes cas que CorrelationPhase::match) : la raison est dans raisonEchec().
    std::optional<RecouvrementReel> estimer(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx);

    // Pourquoi le dernier estimer() a échoué, avec la correction à apporter quand elle est connue. Vide sinon.
    const std::string& raisonEchec() const { return raison; }

    // Rectangles de la zone commune dans A et dans B (même taille), B placée à `translation` arrondie au pixel
    // (coin haut-gauche de B dans le repère de A). Rectangles vides si les tuiles ne se superposent pas.
    static std::pair<cv::Rect, cv::Rect> zonesCommunes(cv::Size a, cv::Size b, cv::Point2d translation);

private:
    static constexpr int PRECISION_PIXEL = 1;   // facteurDft de la première passe : pic entier, sans sous-pixel

    // Translation et recouvrement réel dans le repère des tuiles d'origine, à partir de la mesure faite
    // sur la paire orientée (B en bas ou à droite)
    static RecouvrementReel versTuiles(const correlation_phase::PaireOrientee& paire,
                                       const correlation_phase::Mesure& mesure);

    correlation_phase::Parametres parametres;   // vérifiés une seule fois, dans le constructeur
    std::string raison;                         // message du dernier échec de estimer()
};

} // namespace stitching::matching
