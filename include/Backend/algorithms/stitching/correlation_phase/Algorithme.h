#pragma once
#include "algorithms/stitching/correlation_phase/Types.h"
#include <limits>
#include <optional>
#include <vector>

namespace stitching::matching::correlation_phase {

// Étape 2 : calculs sur des images déjà préparées (gris float64, NaN = vide).
class Algorithme {
public:
    // Translation globale par corrélation de phase sur les bandes. Vide si le pic est ambigu.
    static std::optional<Mesure> mesurerGlobal(const Bandes& bandes, const Parametres& p);

    // À appeler quand mesurerGlobal a échoué : meilleur pic cherché sur toute la surface (sans la marge du rail),
    // pour savoir si la translation réelle est simplement hors de la marge. Précision au pixel.
    static Diagnostic diagnostiquerGlobal(const Bandes& bandes, const Parametres& p);

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
    static constexpr double ETENDUE_DFT         = 1.5;    // px : côté de la grille suréchantillonnée autour du pic entier
    static constexpr double SANS_MARGE          = std::numeric_limits<double>::infinity();  // pic cherché partout

    // ---- Corrélation de phase ----

    // Translation (dy, dx) telle que a(y, x) ≈ b(y - dy, x - dx), pour deux images de même taille.
    // Le pic est cherché parmi les décalages |dx|, |dy| ≤ marge (SANS_MARGE : partout), précision 1/facteurDft px.
    static Correlation correlationDePhase(const cv::Mat& a, const cv::Mat& b, double lissage, double marge,
                                          int facteurDft);

    // Spectre croisé normalisé de deux images de même taille, fenêtre de Hann appliquée
    static cv::Mat spectreCroise(const cv::Mat& a, const cv::Mat& b);

    // Surface de corrélation lissée par un flou gaussien de `lissage` px (non centrée : décalage nul en (0, 0))
    static cv::Mat surfaceLissee(const cv::Mat& croise, double lissage);

    // Spectre d'une image : vides remplis, moyenne retirée, fenêtre de Hann, DFT complexe
    static cv::Mat spectre(const cv::Mat& image, const cv::Mat& fenetre);

    // Fa · conj(Fb) / |Fa · conj(Fb)| : seule la phase (donc la position) est conservée
    static cv::Mat spectreCroiseNormalise(const cv::Mat& Fa, const cv::Mat& Fb);

    // exp(-2π²σ²(u² + v²)) : multiplier le spectre par ce filtre floute la surface (σ = lissage px).
    // Deux canaux identiques, pour multiplier directement un spectre complexe.
    static cv::Mat filtreGaussien(cv::Size taille, double lissage);

    // DFT inverse normalisée, partie réelle
    static cv::Mat partieReelleInverse(const cv::Mat& spectre);

    // Masque 8 bits (taille de la surface) des décalages |dx|, |dy| ≤ marge ; vide si marge = SANS_MARGE
    static cv::Mat zoneAutorisee(cv::Size taille, double marge);

    // Indices (x, y) du maximum de la surface parmi les décalages autorisés (masque vide = partout)
    static cv::Point localiserPic(const cv::Mat& surface, const cv::Mat& autorise);

    // Décalage (dx, dy) sous-pixel du maximum de la surface brute, cherché à ±ETENDUE_DFT/2 px autour du pic
    // entier par DFT suréchantillonnée du spectre croisé (Guizar-Sicairos 2008), au pas de 1/facteur px
    static cv::Point2d raffinerPic(const cv::Mat& croise, cv::Point2d picEntier, int facteur);

    // Noyau complexe (taille × n) : exp(2iπ · k · s_j / n), k fréquence signée, s_j = debut + j · pas.
    // Multiplier un spectre par ce noyau évalue sa transformée inverse aux décalages s_j le long d'un axe.
    static cv::Mat noyauDft(int n, double debut, double pas, int taille);

    // Indice k ramené dans ]-n/2, n/2] (décalage signé, comme np.fft.fftfreq(n) · n)
    static int decalageSigne(int k, int n);

    // Fréquence de l'indice k pour n échantillons (np.fft.fftfreq)
    static double frequence(int k, int n);

    // Hauteur du meilleur pic / celle du second (hors d'un carré de ±rayon px autour du premier),
    // tous deux cherchés dans la zone autorisée de la corrélation
    static double rapportPics(const Correlation& c, int rayon);

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
