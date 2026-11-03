#pragma once
#include "algorithms/egalisation/Types.h"
#include <vector>

namespace stitching::egalisation {

// Étape 2 : calculs sur les luminances réduites.
//
// Modèle (en logarithme), pour chaque pixel du recouvrement de la paire (A, B) :
//     log I_A − log I_B = log g_A − log g_B + c · (r²_A − r²_B)
// g : gain de la tuile ; c : force du champ d'éclairage exp(c · r²), commun à toutes les tuiles (méthode B ;
// c = 0 pour la méthode A). r² = u² + v², u et v dans [−1, 1] : position normalisée du pixel dans sa tuile.
class Algorithme {
public:
    // Un échantillon par pixel de A dont le correspondant dans B (B placée à paire.translation) est dans les
    // deux tuiles, hors de la marge, et ni trop sombre ni saturé dans les deux. Vide si les tuiles ne se
    // recouvrent pas.
    static Echantillons echantillons(const TuileReduite& A, const TuileReduite& B, const PaireVoisine& paire,
                                     const Parametres& p);

    // Moindres carrés robustes (IRLS, poids de Huber) sur tous les échantillons à la fois.
    // Contrainte de jauge : moyenne des log g = 0 (conserve la luminosité globale de l'œuvre).
    // avecChamp = false : méthode A (c = 0) ; true : méthode B.
    static Solution resoudre(const std::vector<Echantillons>& echantillons, int nbTuiles, bool avecChamp,
                             const Parametres& p);

    // RMS et écart absolu médian (en %) de l'écart restant sur les échantillons, après correction par `s`
    // (Solution vide : avant correction). Remplit rmsResidu et residuMedian.
    static void residus(const std::vector<Echantillons>& echantillons, const Solution& s, Bilan& bilan);

    // Écart moyen et variation basse fréquence (en %) dans les recouvrements, sur les luminances réduites
    // corrigées par `s` (Solution vide : avant correction). Remplit ecartMoyen et variationBF.
    static void mesurer(const std::vector<TuileReduite>& tuiles, const std::vector<PaireVoisine>& paires,
                        const Solution& s, Bilan& bilan);

    // Luminance réduite corrigée : lum / (g · exp(c · r²)), ramenée dans [0, 1]
    static cv::Mat corrigerReduite(const cv::Mat& lum, double gain, double c);

private:
    static constexpr double ECHELLE_MAD     = 1.4826;  // MAD → écart-type pour une loi normale
    static constexpr double RESIDU_MIN      = 1e-9;    // évite la division par 0 dans les poids de Huber
    static constexpr float  BAS_MESURE      = 0.02f;   // mesurer() : pixels exclus en dessous / au-dessus
    static constexpr float  HAUT_MESURE     = 0.98f;
    static constexpr int    EROSION_MESURE  = 5;       // px (image réduite) : bord des zones valides retiré
    static constexpr int    NOYAU_BF        = 51;      // px : flou de la variation basse fréquence
    static constexpr double SIGMA_BF        = 12.0;
    static constexpr double POIDS_MIN_BF    = 0.3;     // part minimale de pixels valides sous le noyau

    // r² d'un point (x, y) en pleine résolution, dans une tuile de taille `forme`
    static double r2(double x, double y, cv::Size forme);

    // Valeur au centile q (0 à 100) ; v est réordonné
    static double centile(std::vector<double>& v, double q);
};

} // namespace stitching::egalisation
