#pragma once
#include "algorithms/stitching/correlation_phase/Types.h"
#include <string>
#include <utility>
#include <vector>

namespace stitching::matching::correlation_phase {

// Étape 3 : mise en forme du résultat au format commun des FeatureMatcher (Matches),
// ou du message qui explique pourquoi match() a renvoyé false.
class Sortie {
public:
    // Translation + nbPoints meilleurs points, dans le repère des tuiles d'origine
    // (orientation demandée par le pipeline rétablie). origineA : coin de la zone commune dans A.
    static Matches construire(const PaireOrientee& paire, const Mesure& mesure, cv::Point origineA,
                              std::vector<Correspondance> points, int nbPoints);

    // Échec de Preparation::orienter : tuile vide, tailles différentes ou recouvrement hors de ]0, 1]
    static std::string echecPaire(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx);

    // Échec de Preparation::bandes : pas assez de contenu commun dans les bandes de recouvrement nominal
    static std::string echecBandes(const PairContext& ctx, const Parametres& p);

    // Échec de Algorithme::mesurerGlobal : pic hors de la marge du rail (avec la marge et le recouvrement
    // à essayer), ou pic ambigu
    static std::string echecGlobal(const Diagnostic& d, const PaireOrientee& paire, const Bandes& bandes,
                                   const PairContext& ctx, const Parametres& p);

    // Échec de Preparation::zoneCommune : pas de zone commune exploitable à la translation de la première passe
    // (coin de B dans A, repère des tuiles d'origine)
    static std::string echecZoneCommune(cv::Point2d translation, const Parametres& p);

    // Échec de Algorithme::affinerGlobal : le pic de la deuxième passe touche le bord de sa marge
    static std::string echecAffinage(cv::Point2d translation, const Bandes& zone);

private:
    static constexpr double MARGE_SECURITE = 10.0;  // px ajoutés à la marge conseillée (pic diagnostiqué au pixel près)

    // Nombre écrit avec `decimales` chiffres après la virgule
    static std::string nombre(double valeur, int decimales);

    // Tri par confiance décroissante, puis on garde au plus nbPoints points
    static void trierEtLimiter(std::vector<Correspondance>& points, int nbPoints);

    // (point dans A, point dans B), en coordonnées des tuiles entières
    static std::pair<cv::Point2f, cv::Point2f> versTuiles(const Correspondance& c, cv::Point origineA);

    // A et B avaient été échangées : on inverse la translation et on échange pointsA / pointsB
    static void annulerOrientation(Matches& m);
};

} // namespace stitching::matching::correlation_phase
