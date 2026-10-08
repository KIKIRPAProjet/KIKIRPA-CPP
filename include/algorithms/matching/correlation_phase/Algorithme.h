#pragma once
#include "algorithms/matching/correlation_phase/Types.h"
#include <optional>
#include <vector>

namespace stitching::matching::correlation_phase {

// Étape 2 : calculs sur des images déjà préparées (gris float64, NaN = vide).
class Algorithme {
public:
    // Translation globale par corrélation de phase sur les bandes. Vide si le pic est ambigu.
    static std::optional<Mesure> mesurerGlobal(const Bandes& bandes, const Parametres& p);

    // Points texturés de la zone commune, re-mesurés chacun par une corrélation de phase locale.
    static std::vector<Correspondance> raffiner(const Recouvrement& rec, const Mesure& globale,
                                                const Parametres& p);

private:
    static constexpr double PI                  = 3.14159265358979323846;
    static constexpr double EPSILON             = 1e-12;  // évite la division par 0 dans le spectre croisé
    static constexpr int    RAYON_PIC           = 10;     // px : voisinage du pic exclu pour chercher le second
    static constexpr double QUALITE_COINS       = 0.01;   // goodFeaturesToTrack : fraction du meilleur coin
    static constexpr int    CANDIDATS_PAR_POINT = 3;      // candidats testés pour chaque point demandé
    static constexpr double PART_MIN_VALIDES    = 0.5;    // part minimale de pixels non vides dans un patch

    // ---- Corrélation de phase ----

    // Translation (dy, dx) telle que a(y, x) ≈ b(y - dy, x - dx), pour deux images de même taille
    static Correlation correlationDePhase(const cv::Mat& a, const cv::Mat& b, double lissage);

    // Spectre d'une image : vides remplis, moyenne retirée, fenêtre de Hann, DFT complexe
    static cv::Mat spectre(const cv::Mat& image, const cv::Mat& fenetre);

    // Fa · conj(Fb) / |Fa · conj(Fb)| : seule la phase (donc la position) est conservée
    static cv::Mat spectreCroiseNormalise(const cv::Mat& Fa, const cv::Mat& Fb);

    // exp(-2π²σ²(u² + v²)) : multiplier le spectre par ce filtre floute la surface (σ = lissage px).
    // Deux canaux identiques, pour multiplier directement un spectre complexe.
    static cv::Mat filtreGaussien(cv::Size taille, double lissage);

    // DFT inverse normalisée, partie réelle
    static cv::Mat partieReelleInverse(const cv::Mat& spectre);

    // Décalage (dx, dy) du maximum de la surface, au sous-pixel, ramené dans ]-taille/2, taille/2].
    // `pixel` reçoit la position entière du maximum.
    static cv::Point2d localiserPic(const cv::Mat& surface, cv::Point& pixel);

    // Sommet de la parabole passant par trois valeurs voisines (entre -0.5 et +0.5)
    static double sousPixel(double avant, double centre, double apres);

    // Fréquence de l'indice k pour n échantillons (np.fft.fftfreq)
    static double frequence(int k, int n);

    // Hauteur du meilleur pic / celle du second (hors d'un carré de ±rayon px autour du premier)
    static double rapportPics(const cv::Mat& surface, int rayon);

    // ---- Raffinement local ----

    // Coins texturés et espacés, assez loin des bords pour que patch + correction restent dans la zone
    static std::vector<cv::Point2f> candidats(const Recouvrement& rec, const Parametres& p);

    // Translation locale autour d'un candidat ; vide si le patch est trop vide, incohérent
    // avec la translation globale ou ambigu
    static std::optional<Correspondance> mesurerPoint(const Recouvrement& rec, cv::Point2f centre,
                                                      const Mesure& globale, const Parametres& p);

    // Corrélation normalisée entre deux patchs de même taille, ramenée dans [0, 1]
    static float ncc(const cv::Mat& patchA, const cv::Mat& patchB);
};

} // namespace stitching::matching::correlation_phase
