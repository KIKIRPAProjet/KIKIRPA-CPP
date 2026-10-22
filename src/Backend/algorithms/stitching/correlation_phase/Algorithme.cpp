//Algorithme.cpp
#include "algorithms/stitching/correlation_phase/Algorithme.h"
#include "algorithms/stitching/correlation_phase/Preparation.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/features2d.hpp>

namespace stitching::matching::correlation_phase {

// ======================================== Public ========================================

std::optional<Mesure> Algorithme::mesurerGlobal(const Bandes& bandes, const Parametres& p) {
    const Correlation c = correlationDePhase(bandes.a, bandes.b, p.lissage, bandes.marge, p.facteurDft);
    const double rapport = rapportPics(c, RAYON_PIC);
    if (rapport < p.seuilRapport) return std::nullopt;   // deux positions se valent : mesure ambiguë
    return Mesure{bandes.origine.x + c.dx, bandes.origine.y + c.dy, rapport};
}

std::optional<Mesure> Algorithme::affinerGlobal(const Bandes& zone, const Parametres& p) {
    const Correlation c = correlationDePhase(zone.a, zone.b, p.lissage, zone.marge, p.facteurDft);
    // Pic entier sur le bord de la marge : le vrai maximum est sans doute au-delà, la première passe s'est trompée
    // de plus que la marge. Mieux vaut échouer que renvoyer une translation tronquée au bord. Le test porte sur
    // le pic entier : le raffinement peut ramener un pic de bord jusqu'à 0,75 px vers l'intérieur.
    const cv::Point pixel = localiserPic(c.surface, c.autorise);
    const int bord = static_cast<int>(std::floor(zone.marge));
    if (std::abs(decalageSigne(pixel.x, c.surface.cols)) >= bord || std::abs(decalageSigne(pixel.y, c.surface.rows)) >= bord)
        return std::nullopt;
    // Le second pic n'est pas contrôlé : la marge (quelques px) est plus petite que le voisinage exclu autour
    // du pic, le test passerait toujours. L'ambiguïté a été contrôlée par la première passe.
    return Mesure{zone.origine.x + c.dx, zone.origine.y + c.dy, std::numeric_limits<double>::quiet_NaN()};
}

Diagnostic Algorithme::diagnostiquerGlobal(const Bandes& bandes, const Parametres& p) {
    // Même surface que mesurerGlobal, sans sous-pixel : seul l'endroit du pic compte pour expliquer l'échec.
    // Le surcoût (une corrélation de plus) n'est payé que lorsque la mesure a déjà échoué.
    const cv::Mat lisse = surfaceLissee(spectreCroise(bandes.a, bandes.b), p.lissage);
    const cv::Mat dansMarge = zoneAutorisee(lisse.size(), bandes.marge);
    const cv::Point pixel = localiserPic(lisse, cv::Mat());
    const cv::Point2d libre(decalageSigne(pixel.x, lisse.cols), decalageSigne(pixel.y, lisse.rows));

    Diagnostic d;
    d.rapportMarge = rapportPics(Correlation{0, 0, 0, lisse, dansMarge}, RAYON_PIC);
    d.picLibre     = libre;
    d.rapportLibre = rapportPics(Correlation{libre.y, libre.x, 0, lisse, cv::Mat()}, RAYON_PIC);
    d.horsMarge    = std::max(std::abs(libre.x), std::abs(libre.y)) > bandes.marge;
    return d;
}

std::vector<Correspondance> Algorithme::raffiner(const Recouvrement& rec, const Mesure& globale,
                                                 const Parametres& p) {
    std::vector<Correspondance> points;
    for (const cv::Point2f& centre : candidats(rec, p))
        if (const auto point = mesurerPoint(rec, centre, globale, p)) points.push_back(*point);
    return points;
}

// ======================================== Private : corrélation de phase ========================================

Correlation Algorithme::correlationDePhase(const cv::Mat& a, const cv::Mat& b, double lissage, double marge,
                                           int facteurDft) {
    const cv::Mat croise = spectreCroise(a, b);
    const cv::Mat brute = partieReelleInverse(croise);
    const cv::Mat lisse = surfaceLissee(croise, lissage);

    // Le pic entier est cherché sur la surface lissée : il est plus large mais reste symétrique autour du vrai
    // décalage. Sur la surface brute, quand le pic est faible, le pixel le plus haut est parfois un grain
    // de bruit voisin (erreur de 1 à 2 px).
    // Seulement dans la marge du rail : un pic plus haut au-delà ne peut pas être le bon (motif répétitif,
    // fond uni), le rail ne peut pas s'être trompé d'autant.
    const cv::Mat autorise = zoneAutorisee(a.size(), marge);
    const cv::Point pixel = localiserPic(lisse, autorise);
    const cv::Point2d entier(decalageSigne(pixel.x, a.cols), decalageSigne(pixel.y, a.rows));

    // La fraction de pixel est calculée sur le spectre brut : le lissage déplace légèrement le sommet du pic,
    // et une parabole sur trois valeurs entières est tirée vers l'entier le plus proche.
    // Au pixel près (première passe), on garde le pic de la surface lissée : celui de la surface brute est
    // parfois un grain de bruit voisin.
    const cv::Point2d decalage = facteurDft > 1 ? raffinerPic(croise, entier, facteurDft) : entier;
    return Correlation{decalage.y, decalage.x, brute.at<double>(pixel), lisse, autorise};
}

cv::Mat Algorithme::spectreCroise(const cv::Mat& a, const cv::Mat& b) {
    // Fenêtre de Hann : sans elle, les bords de l'image créent un faux pic en (0, 0)
    cv::Mat fenetre;
    cv::createHanningWindow(fenetre, a.size(), CV_64F);
    return spectreCroiseNormalise(spectre(a, fenetre), spectre(b, fenetre));
}

cv::Mat Algorithme::surfaceLissee(const cv::Mat& croise, double lissage) {
    // La surface brute donne le même poids à toutes les fréquences. Le grain du capteur et les motifs
    // fixes de l'acquisition (hautes fréquences) y créent de faux pics fins, parfois plus hauts que le vrai.
    // Lisser la surface revient à réduire le poids des hautes fréquences : le vrai pic, porté aussi
    // par les basses fréquences, ressort.
    return partieReelleInverse(croise.mul(filtreGaussien(croise.size(), lissage)));
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

cv::Mat Algorithme::zoneAutorisee(cv::Size taille, double marge) {
    if (!std::isfinite(marge)) return {};
    // La surface n'est pas centrée : l'indice k correspond au décalage signé decalageSigne(k, n)
    cv::Mat autorise(taille, CV_8U);
    for (int y = 0; y < taille.height; ++y) {
        auto* ligne = autorise.ptr<uchar>(y);
        const bool ligneDansMarge = std::abs(decalageSigne(y, taille.height)) <= marge;
        for (int x = 0; x < taille.width; ++x)
            ligne[x] = ligneDansMarge && std::abs(decalageSigne(x, taille.width)) <= marge ? 255 : 0;
    }
    return autorise;
}

cv::Point Algorithme::localiserPic(const cv::Mat& surface, const cv::Mat& autorise) {
    cv::Point pixel;
    cv::minMaxLoc(surface, nullptr, nullptr, nullptr, &pixel, autorise);
    return pixel;
}

cv::Point2d Algorithme::raffinerPic(const cv::Mat& croise, cv::Point2d picEntier, int facteur) {
    // Suréchantillonner toute la surface (zéro-padding ×facteur) serait hors de portée sur des bandes de plusieurs
    // milliers de px. On évalue exactement la transformée inverse sur une petite grille autour du pic entier, par
    // deux produits matriciels : grille = Ky · croise · Kxᵀ, coût proportionnel à (taille de la grille) × pixels.
    const int taille = static_cast<int>(std::ceil(ETENDUE_DFT * facteur));
    const double pas = 1.0 / facteur;
    const double debutX = picEntier.x - (taille / 2) * pas;
    const double debutY = picEntier.y - (taille / 2) * pas;

    cv::Mat lignes, grille;
    cv::gemm(noyauDft(croise.rows, debutY, pas, taille), croise, 1.0, cv::noArray(), 0.0, lignes);
    cv::gemm(lignes, noyauDft(croise.cols, debutX, pas, taille).t(), 1.0, cv::noArray(), 0.0, grille);

    // Le facteur 1/N de la transformée inverse ne change pas la position du maximum
    cv::Mat reel;
    cv::extractChannel(grille, reel, 0);
    cv::Point meilleur;
    cv::minMaxLoc(reel, nullptr, nullptr, nullptr, &meilleur);
    return {debutX + meilleur.x * pas, debutY + meilleur.y * pas};
}

cv::Mat Algorithme::noyauDft(int n, double debut, double pas, int taille) {
    // Fréquences signées : pour un décalage non entier, c'est l'interpolation de Fourier (la même que np.fft)
    cv::Mat noyau(taille, n, CV_64FC2);
    for (int j = 0; j < taille; ++j) {
        auto* ligne = noyau.ptr<cv::Vec2d>(j);
        const double s = debut + j * pas;
        for (int k = 0; k < n; ++k) {
            const double angle = 2 * PI * decalageSigne(k, n) * s / n;
            ligne[k] = cv::Vec2d(std::cos(angle), std::sin(angle));
        }
    }
    return noyau;
}

int Algorithme::decalageSigne(int k, int n) {
    // La transformée de Fourier est périodique : un indice au-delà de la moitié est un décalage négatif
    return k <= (n - 1) / 2 ? k : k - n;
}

double Algorithme::frequence(int k, int n) {
    return decalageSigne(k, n) / static_cast<double>(n);
}

double Algorithme::rapportPics(const Correlation& c, int rayon) {
    // Proche de 1 : deux positions se valent, la mesure est ambiguë. Nettement au-dessus de 1 : pic franc.
    const cv::Mat& surface = c.surface;
    const int h = surface.rows, w = surface.cols;
    double pic;
    cv::Point p;
    cv::minMaxLoc(surface, nullptr, &pic, nullptr, &p, c.autorise);

    // Le second pic est cherché lui aussi dans la marge : un pic hors marge ne rend pas la mesure ambiguë,
    // puisqu'il ne peut pas être le bon.
    cv::Mat masquee = surface.clone();
    if (!c.autorise.empty()) masquee.setTo(-std::numeric_limits<double>::infinity(), ~c.autorise);

    // La surface est périodique : un pic collé à un bord déborde de l'autre côté.
    // Même voisinage que le Python (pic ramené au centre, carré tronqué aux bords), en indices modulo.
    const int cy = h / 2, cx = w / 2;
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

    // Résidu local par rapport à la translation arrondie : pa(y, x) ≈ pb(y - dy, x - dx).
    // Pic cherché partout, sans marge : limiter la recherche à ±ecartMax viderait le test du second pic
    // (tout le voisinage serait masqué) ; un pic local qui s'écarte de la globale est rejeté juste après.
    const Correlation local = correlationDePhase(pa, pb, p.lissageLocal, SANS_MARGE, p.facteurDft);
    const cv::Point2d translation(rec.decalage.x + local.dx, rec.decalage.y + local.dy);
    if (std::abs(translation.x - globale.tx) > p.ecartMax || std::abs(translation.y - globale.ty) > p.ecartMax)
        return std::nullopt;
    if (rapportPics(local, RAYON_PIC) < p.seuilRapport) return std::nullopt;

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
