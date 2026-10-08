#include "algorithms/matching/correlation_phase/Algorithme.h"
#include "algorithms/matching/correlation_phase/Preparation.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stitching::matching::correlation_phase {

// ======================================== Public ========================================

std::optional<Mesure> Algorithme::mesurerGlobal(const Bandes& bandes, const Parametres& p) {
    const Correlation c = correlationDePhase(bandes.a, bandes.b, p.lissage);
    const double rapport = rapportPics(c.surface, RAYON_PIC);
    if (rapport < p.seuilRapport) return std::nullopt;   // deux positions se valent : mesure ambiguë
    return Mesure{bandes.origine.x + c.dx, bandes.origine.y + c.dy, rapport};
}

std::vector<Correspondance> Algorithme::raffiner(const Recouvrement& rec, const Mesure& globale,
                                                 const Parametres& p) {
    std::vector<Correspondance> points;
    for (const cv::Point2f& centre : candidats(rec, p))
        if (const auto point = mesurerPoint(rec, centre, globale, p)) points.push_back(*point);
    return points;
}

// ======================================== Private : corrélation de phase ========================================

Correlation Algorithme::correlationDePhase(const cv::Mat& a, const cv::Mat& b, double lissage) {
    // Fenêtre de Hann : sans elle, les bords de l'image créent un faux pic en (0, 0)
    cv::Mat fenetre;
    cv::createHanningWindow(fenetre, a.size(), CV_64F);
    const cv::Mat croise = spectreCroiseNormalise(spectre(a, fenetre), spectre(b, fenetre));

    // La surface brute donne le même poids à toutes les fréquences. Le grain du capteur et les motifs
    // fixes de l'acquisition (hautes fréquences) y créent de faux pics fins, parfois plus hauts que le vrai.
    // Lisser la surface revient à réduire le poids des hautes fréquences : le vrai pic, porté aussi
    // par les basses fréquences, ressort.
    const cv::Mat brute = partieReelleInverse(croise);
    const cv::Mat lisse = partieReelleInverse(croise.mul(filtreGaussien(a.size(), lissage)));

    // Le pic est cherché sur la surface lissée : il est plus large mais reste symétrique autour du vrai
    // décalage. Sur la surface brute, quand le pic est faible, le pixel le plus haut est parfois un grain
    // de bruit voisin (erreur de 1 à 2 px).
    cv::Point pixel;
    const cv::Point2d decalage = localiserPic(lisse, pixel);
    return Correlation{decalage.y, decalage.x, brute.at<double>(pixel), lisse};
}

cv::Mat Algorithme::spectre(const cv::Mat& image, const cv::Mat& fenetre) {
    const cv::Mat plein = Preparation::remplirVides(image);
    cv::Mat F;
    cv::dft((plein - cv::mean(plein)[0]).mul(fenetre), F, cv::DFT_COMPLEX_OUTPUT);
    return F;
}

cv::Mat Algorithme::spectreCroiseNormalise(const cv::Mat& Fa, const cv::Mat& Fb) {
    cv::Mat croise;
    cv::mulSpectrums(Fa, Fb, croise, 0, /*conjB=*/true);
    for (int y = 0; y < croise.rows; ++y) {
        auto* ligne = croise.ptr<cv::Vec2d>(y);
        for (int x = 0; x < croise.cols; ++x)
            ligne[x] /= std::hypot(ligne[x][0], ligne[x][1]) + EPSILON;
    }
    return croise;
}

cv::Mat Algorithme::filtreGaussien(cv::Size taille, double lissage) {
    cv::Mat filtre(taille, CV_64FC2);
    const double k = -2 * PI * PI * lissage * lissage;
    for (int y = 0; y < taille.height; ++y) {
        auto* ligne = filtre.ptr<cv::Vec2d>(y);
        const double u = frequence(y, taille.height);
        for (int x = 0; x < taille.width; ++x) {
            const double v = frequence(x, taille.width);
            const double g = std::exp(k * (u * u + v * v));
            ligne[x] = cv::Vec2d(g, g);
        }
    }
    return filtre;
}

cv::Mat Algorithme::partieReelleInverse(const cv::Mat& spectre) {
    cv::Mat complexe, reel;
    cv::idft(spectre, complexe, cv::DFT_SCALE);
    cv::extractChannel(complexe, reel, 0);
    return reel;
}

cv::Point2d Algorithme::localiserPic(const cv::Mat& surface, cv::Point& pixel) {
    const int h = surface.rows, w = surface.cols;
    cv::minMaxLoc(surface, nullptr, nullptr, nullptr, &pixel);
    const int py = pixel.y, px = pixel.x;

    // Pixel le plus haut, puis sommet de la parabole pour la fraction.
    // La surface est périodique : les voisins d'un pic collé au bord sont de l'autre côté.
    const double centre = surface.at<double>(py, px);
    double dy = py + sousPixel(surface.at<double>((py - 1 + h) % h, px), centre, surface.at<double>((py + 1) % h, px));
    double dx = px + sousPixel(surface.at<double>(py, (px - 1 + w) % w), centre, surface.at<double>(py, (px + 1) % w));

    // La transformée de Fourier est périodique : un pic au-delà de la moitié est un décalage négatif
    if (dy > h / 2.0) dy -= h;
    if (dx > w / 2.0) dx -= w;
    return {dx, dy};
}

double Algorithme::sousPixel(double avant, double centre, double apres) {
    const double courbure = avant - 2 * centre + apres;
    return courbure == 0 ? 0.0 : 0.5 * (avant - apres) / courbure;
}

double Algorithme::frequence(int k, int n) {
    return (k <= (n - 1) / 2 ? k : k - n) / static_cast<double>(n);
}

double Algorithme::rapportPics(const cv::Mat& surface, int rayon) {
    // Proche de 1 : deux positions se valent, la mesure est ambiguë. Nettement au-dessus de 1 : pic franc.
    const int h = surface.rows, w = surface.cols;
    double pic;
    cv::Point p;
    cv::minMaxLoc(surface, nullptr, &pic, nullptr, &p);

    // La surface est périodique : un pic collé à un bord déborde de l'autre côté.
    // Même voisinage que le Python (pic ramené au centre, carré tronqué aux bords), en indices modulo.
    const int cy = h / 2, cx = w / 2;
    cv::Mat masquee = surface.clone();
    for (int oy = std::max(-rayon, -cy); oy <= std::min(rayon, h - 1 - cy); ++oy)
        for (int ox = std::max(-rayon, -cx); ox <= std::min(rayon, w - 1 - cx); ++ox)
            masquee.at<double>((p.y + oy + h) % h, (p.x + ox + w) % w) = -std::numeric_limits<double>::infinity();

    double second;
    cv::minMaxLoc(masquee, nullptr, &second);
    return second <= 0 ? std::numeric_limits<double>::infinity() : pic / second;
}

// ======================================== Private : raffinement local ========================================

std::vector<cv::Point2f> Algorithme::candidats(const Recouvrement& rec, const Parametres& p) {
    if (p.nbPoints <= 0) return {};   // goodFeaturesToTrack interprète 0 comme « sans limite »
    cv::Mat masque = Preparation::valides(rec.a);
    masque.rowRange(0, rec.marge).setTo(0);
    masque.rowRange(masque.rows - rec.marge, masque.rows).setTo(0);
    masque.colRange(0, rec.marge).setTo(0);
    masque.colRange(masque.cols - rec.marge, masque.cols).setTo(0);
    if (cv::countNonZero(masque) == 0) return {};

    cv::Mat image;
    Preparation::remplirVides(rec.a).convertTo(image, CV_32F);
    std::vector<cv::Point2f> coins;
    cv::goodFeaturesToTrack(image, coins, CANDIDATS_PAR_POINT * p.nbPoints, QUALITE_COINS, p.espacementMin, masque);
    return coins;
}

std::optional<Correspondance> Algorithme::mesurerPoint(const Recouvrement& rec, cv::Point2f centre,
                                                       const Mesure& globale, const Parametres& p) {
    const int f = p.fenetre;
    const cv::Rect patch(static_cast<int>(centre.x) - f / 2, static_cast<int>(centre.y) - f / 2, f, f);
    const cv::Mat pa = rec.a(patch), pb = rec.b(patch);
    const int minValides = static_cast<int>(PART_MIN_VALIDES * f * f);
    if (cv::countNonZero(Preparation::valides(pa)) < minValides ||
        cv::countNonZero(Preparation::valides(pb)) < minValides) return std::nullopt;

    // Résidu local par rapport à la translation arrondie : pa(y, x) ≈ pb(y - dy, x - dx)
    const Correlation local = correlationDePhase(pa, pb, p.lissageLocal);
    const cv::Point2d translation(rec.decalage.x + local.dx, rec.decalage.y + local.dy);
    if (std::abs(translation.x - globale.tx) > p.ecartMax || std::abs(translation.y - globale.ty) > p.ecartMax)
        return std::nullopt;
    if (rapportPics(local.surface, RAYON_PIC) < p.seuilRapport) return std::nullopt;

    // Confiance mesurée sur le patch de B recalé au pixel près
    const cv::Rect patchB = patch - cv::Point(static_cast<int>(std::lround(local.dx)),
                                              static_cast<int>(std::lround(local.dy)));
    return Correspondance{centre, translation, ncc(pa, rec.b(patchB))};
}

float Algorithme::ncc(const cv::Mat& patchA, const cv::Mat& patchB) {
    cv::Mat fa, fb, resultat;
    Preparation::remplirVides(patchA).convertTo(fa, CV_32F);
    Preparation::remplirVides(patchB).convertTo(fb, CV_32F);
    cv::matchTemplate(fa, fb, resultat, cv::TM_CCOEFF_NORMED);
    const float valeur = resultat.at<float>(0, 0);
    return std::isfinite(valeur) ? std::clamp(valeur, 0.f, 1.f) : 0.f;   // patch uniforme : NCC indéfinie
}

} // namespace stitching::matching::correlation_phase
