#pragma once
#include "algorithms/stitching/correlation_phase/Types.h"
#include <optional>

namespace stitching::matching::correlation_phase {

// Étape 1 : des tuiles brutes (8 ou 16 bits, gris ou couleur) aux images de travail
// (gris float64, NaN = pixel vide). Aucune conversion en 8 bits, les tuiles ne sont jamais modifiées.
class Preparation {
public:
    // Lève std::invalid_argument si un paramètre est hors de son domaine (fenetre < 8, valeur négative…)
    static void verifierParametres(const Parametres& p);

    // Vérifie la paire, la ramène au cas « B en bas / à droite » (échange A et B si besoin)
    // et calcule les zones extérieures. Vide si les tuiles sont vides, de tailles différentes,
    // ou si le recouvrement n'est pas dans ]0, 1].
    static std::optional<PaireOrientee> orienter(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx);

    // Bandes censées se recouvrir (recouvrement nominal), en gris, découpées sur la zone où
    // les deux ont du contenu, avec la marge du rail. Vide si pas assez de contenu commun
    // (moins de p.fractionValideMin pixels exploitables dans les deux bandes, ou zone trop petite).
    static std::optional<Bandes> bandes(const PaireOrientee& paire, double recouvrement, const Parametres& p);

    // Zone commune réelle une fois B placée à la translation globale. Vide si elle est trop étroite
    // pour y placer des patchs de p.fenetre px (avec la marge de correction p.ecartMax).
    static std::optional<Recouvrement> recouvrement(const PaireOrientee& paire, const Mesure& mesure,
                                                    const Parametres& p);

    // Masque 8 bits des pixels non NaN
    static cv::Mat valides(const cv::Mat& image);

    // Copie où les pixels NaN sont remplacés par la moyenne des pixels valides (valeur neutre)
    static cv::Mat remplirVides(const cv::Mat& image);

private:
    // Seuils de zoneAvecContenu (part de pixels pleins)
    static constexpr double PART_MIN_COLONNE = 0.1;   // en dessous : colonne presque vide, retirée
    static constexpr double PART_MIN_LIGNE   = 0.5;   // en dessous : ligne / colonne à moitié vide, retirée
    static constexpr int    COTE_MIN_CONTENU = 32;    // px : zone commune plus petite = mesure peu fiable
    static constexpr double MARGE_PART_TUILE = 0.05;  // marge du rail par défaut : part de la tuile dans le sens du déplacement
    static constexpr double MARGE_MIN        = 48.0;  // px : marge du rail par défaut minimale
    static constexpr double PART_SATURATION  = 0.998; // gris ≥ cette part du maximum du type : pixel saturé (reflet)
    static constexpr int    EROSION          = 4;     // px rognés autour des pixels vides ou saturés

    // Côté opposé (Top ↔ Bottom, Left ↔ Right)
    static Direction oppose(Direction d);

    // Rectangle de la bande de recouvrement d'une tuile de taille `forme` sur le côté `cote`
    static cv::Rect bande(cv::Size forme, Direction cote, double recouvrement);

    // Écart maximal (px) entre la translation globale et le recouvrement nominal : p.marge si > 0,
    // sinon MARGE_PART_TUILE de la tuile dans le sens du déplacement, au moins MARGE_MIN
    static double margeRail(const PaireOrientee& paire, const Parametres& p);

    // Masque des pixels à 0 (tous les canaux) reliés au bord de la tuile : la zone hors de l'œuvre
    static cv::Mat zoneExterieure(const cv::Mat& tuile);

    // Gris float64 (moyenne des canaux) ; zone extérieure et pixels saturés → NaN, puis NaN étendu de
    // EROSION px ; zone uniforme → tout NaN
    static cv::Mat enGris(const cv::Mat& tuile, const cv::Mat& exterieur);

    // Plus grande valeur d'un pixel pour la profondeur OpenCV `profondeur` (255, 65535… ; 1 pour un flottant)
    static double valeurMax(int profondeur);

    // Rectangle où les deux images ont du contenu ; vide si moins de 32 px de côté
    static std::optional<cv::Rect> zoneAvecContenu(const cv::Mat& a, const cv::Mat& b);
};

} // namespace stitching::matching::correlation_phase
