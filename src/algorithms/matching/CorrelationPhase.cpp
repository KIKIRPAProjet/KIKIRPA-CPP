#include "algorithms/matching/CorrelationPhase.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace stitching::matching {
namespace {

constexpr double PI = 3.14159265358979323846;

// Pixels valides (non NaN) : NaN != NaN
cv::Mat valides(const cv::Mat& m) { return m == m; }

// Copie de travail en gris, float64. Les pixels à 0 (zone noire hors de l'œuvre) sont mis à NaN : ce ne sont pas des données.
// Attention : un pixel réellement noir (ombre bouchée à 0) est traité de la même façon.
// Pas de normalisation par percentiles comme dans charger() : la corrélation de phase (phase seule)
// et la NCC sont insensibles à une transformation affine de l'intensité.
cv::Mat enGris(const cv::Mat& tuile) {
    cv::Mat lum;
    tuile.convertTo(lum, CV_64F);
    if (lum.channels() > 1) {             // tuile couleur : on passe en gris (moyenne des canaux)
        cv::Mat gris;
        cv::transform(lum, gris, cv::Mat(1, lum.channels(), CV_64F, cv::Scalar(1.0 / lum.channels())));
        lum = gris;
    }
    lum.setTo(std::numeric_limits<double>::quiet_NaN(), lum == 0);
    double bas = 0, haut = 0;
    cv::Mat masque = valides(lum);
    if (cv::countNonZero(masque) > 0) cv::minMaxLoc(lum, &bas, &haut, nullptr, nullptr, masque);
    if (haut == bas)                      // zone vide ou uniforme : aucune information de position
        lum.setTo(std::numeric_limits<double>::quiet_NaN());
    return lum;
}

// Pixels vides : valeur neutre (moyenne des pixels valides)
cv::Mat sansNaN(const cv::Mat& m) {
    cv::Mat masque = valides(m);
    cv::Mat r = m.clone();
    r.setTo(cv::mean(m, masque)[0], ~masque);
    return r;
}

Direction oppose(Direction d) {
    switch (d) {
        case Direction::Top:    return Direction::Bottom;
        case Direction::Bottom: return Direction::Top;
        case Direction::Left:   return Direction::Right;
        case Direction::Right:  return Direction::Left;
    }
    return d;
}

// Bande de recouvrement d'une tuile de taille `forme` sur un côté
cv::Rect bande(cv::Size forme, Direction cote, double recouvrement) {
    const int h = forme.height, w = forme.width;
    const int bh = static_cast<int>(h * recouvrement), bw = static_cast<int>(w * recouvrement);
    switch (cote) {
        case Direction::Top:    return {0, 0, w, bh};
        case Direction::Bottom: return {0, h - bh, w, bh};
        case Direction::Left:   return {0, 0, bw, h};
        case Direction::Right:  return {w - bw, 0, bw, h};
    }
    return {};
}

// Rectangle où les deux bandes ont du contenu (pixels non vides).
// Le bord d'une zone noire est au même endroit dans les deux tuiles : on le coupe,
// sinon il crée un faux pic à décalage nul. Vide si pas assez de contenu commun (PasDeContenu en Python).
std::optional<cv::Rect> zoneAvecContenu(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat plein;
    cv::Mat(valides(a) & valides(b)).convertTo(plein, CV_64F, 1.0 / 255);
    const int h = plein.rows, w = plein.cols;

    std::vector<int> colonnes, lignes;
    cv::Mat moyenneColonnes;
    cv::reduce(plein, moyenneColonnes, 0, cv::REDUCE_AVG);
    for (int x = 0; x < w; ++x)
        if (moyenneColonnes.at<double>(0, x) > 0.1) colonnes.push_back(x);       // colonnes presque vides
    if (!colonnes.empty()) {
        for (int y = 0; y < h; ++y) {
            const double* ligne = plein.ptr<double>(y);
            double somme = 0;
            for (int x : colonnes) somme += ligne[x];
            if (somme / colonnes.size() > 0.5) lignes.push_back(y);
        }
    }
    if (!lignes.empty()) {                                                        // puis colonnes à moitié vides
        std::vector<double> somme(w, 0.0);
        for (int y : lignes) {
            const double* ligne = plein.ptr<double>(y);
            for (int x = 0; x < w; ++x) somme[x] += ligne[x];
        }
        colonnes.clear();
        for (int x = 0; x < w; ++x)
            if (somme[x] / lignes.size() > 0.5) colonnes.push_back(x);
    }
    if (lignes.empty() || colonnes.empty()) return std::nullopt;
    if (lignes.back() - lignes.front() < 32 || colonnes.back() - colonnes.front() < 32)
        return std::nullopt;                       // zone commune trop petite pour une mesure fiable
    return cv::Rect(colonnes.front(), lignes.front(),
                    colonnes.back() - colonnes.front() + 1, lignes.back() - lignes.front() + 1);
}

// Sommet de la parabole passant par trois valeurs voisines (décalage entre -0.5 et +0.5)
double sousPixel(double avant, double centre, double apres) {
    const double courbure = avant - 2 * centre + apres;
    return courbure == 0 ? 0.0 : 0.5 * (avant - apres) / courbure;
}

// Fréquence de l'indice k pour un signal de n échantillons (np.fft.fftfreq)
double frequence(int k, int n) { return (k <= (n - 1) / 2 ? k : k - n) / static_cast<double>(n); }

struct Correlation {
    double dy, dx;   // a(y, x) ≈ b(y - dy, x - dx)
    double pic;      // hauteur du pic sur la surface brute
    cv::Mat lisse;   // surface lissée (non centrée : décalage nul en (0, 0))
};

// Translation (dy, dx) telle que a(y, x) ≈ b(y - dy, x - dx), pour deux images de même taille.
// lissage : sigma (px) du flou gaussien appliqué à la surface de corrélation pour localiser le pic.
Correlation correlationDePhase(const cv::Mat& a0, const cv::Mat& b0, double lissage) {
    const int h = a0.rows, w = a0.cols;
    cv::Mat a = sansNaN(a0), b = sansNaN(b0);
    // Fenêtre de Hann : sans elle, les bords de l'image créent un faux pic en (0, 0)
    cv::Mat fenetre;
    cv::createHanningWindow(fenetre, a.size(), CV_64F);
    cv::Mat Fa, Fb;
    cv::dft((a - cv::mean(a)[0]).mul(fenetre), Fa, cv::DFT_COMPLEX_OUTPUT);
    cv::dft((b - cv::mean(b)[0]).mul(fenetre), Fb, cv::DFT_COMPLEX_OUTPUT);

    // Spectre croisé normalisé : on ne garde que la phase, donc seulement l'information de position
    cv::Mat croise;
    cv::mulSpectrums(Fa, Fb, croise, 0, /*conjB=*/true);
    // La surface brute donne le même poids à toutes les fréquences. Le grain du capteur et les motifs
    // fixes de l'acquisition (hautes fréquences) y créent de faux pics fins, parfois plus hauts que le vrai.
    // Lisser la surface revient à réduire le poids des hautes fréquences : le vrai pic, porté aussi
    // par les basses fréquences, ressort. Multiplier le spectre par cette gaussienne équivaut à
    // flouter la surface avec une gaussienne d'écart-type `lissage` pixels.
    cv::Mat croiseLisse(croise.size(), croise.type());
    const double k = -2 * PI * PI * lissage * lissage;
    for (int y = 0; y < h; ++y) {
        auto* c = croise.ptr<cv::Vec2d>(y);
        auto* cl = croiseLisse.ptr<cv::Vec2d>(y);
        const double u = frequence(y, h);
        for (int x = 0; x < w; ++x) {
            c[x] /= std::hypot(c[x][0], c[x][1]) + 1e-12;
            const double v = frequence(x, w);
            cl[x] = c[x] * std::exp(k * (u * u + v * v));
        }
    }
    cv::Mat z, brute, lisse;
    cv::idft(croise, z, cv::DFT_SCALE);
    cv::extractChannel(z, brute, 0);
    cv::idft(croiseLisse, z, cv::DFT_SCALE);
    cv::extractChannel(z, lisse, 0);

    // Position du pic sur la surface lissée : pixel le plus haut, puis sommet de la parabole pour la fraction.
    // Le pic lissé est plus large mais reste symétrique autour du vrai décalage, donc le sommet est aussi précis.
    // Sur la surface brute, quand le pic est faible, le pixel le plus haut est parfois un grain de bruit voisin.
    cv::Point maximum;
    cv::minMaxLoc(lisse, nullptr, nullptr, nullptr, &maximum);
    const int py = maximum.y, px = maximum.x;
    auto L = [&](int y, int x) { return lisse.at<double>((y + h) % h, (x + w) % w); };
    double dy = py + sousPixel(L(py - 1, px), L(py, px), L(py + 1, px));
    double dx = px + sousPixel(L(py, px - 1), L(py, px), L(py, px + 1));

    // La transformée de Fourier est périodique : un pic au-delà de la moitié est un décalage négatif
    if (dy > h / 2.0) dy -= h;
    if (dx > w / 2.0) dx -= w;
    return {dy, dx, brute.at<double>(py, px), lisse};
}

// Hauteur du meilleur pic divisée par celle du second (cherché hors d'un carré de ±rayon px autour du premier).
// Proche de 1 : deux positions se valent, la mesure est ambiguë. Nettement au-dessus de 1 : pic franc.
double rapportPics(const cv::Mat& surface, int rayon = 10) {
    const int h = surface.rows, w = surface.cols;
    double pic;
    cv::Point p;
    cv::minMaxLoc(surface, nullptr, &pic, nullptr, &p);
    // La surface est périodique : un pic collé à un bord déborde de l'autre côté.
    // Même voisinage que Python (pic ramené au centre, carré tronqué aux bords), en indices modulo.
    const int cy = h / 2, cx = w / 2;
    cv::Mat s = surface.clone();
    for (int oy = std::max(-rayon, -cy); oy <= std::min(rayon, h - 1 - cy); ++oy)
        for (int ox = std::max(-rayon, -cx); ox <= std::min(rayon, w - 1 - cx); ++ox)
            s.at<double>((p.y + oy + h) % h, (p.x + ox + w) % w) = -std::numeric_limits<double>::infinity();
    double second;
    cv::minMaxLoc(s, nullptr, &second);
    return second <= 0 ? std::numeric_limits<double>::infinity() : pic / second;
}

struct Decalage { double ty, tx, rapport; };

// Position (ty, tx) du coin haut-gauche de B dans le repère de A, pour B en bas ou à droite de A.
// On corrèle seulement les deux bandes censées se recouvrir (recouvrement nominal du rail),
// pas les tuiles entières : ailleurs, le contenu est différent et brouillerait le pic.
std::optional<Decalage> mesurerDecalage(const cv::Mat& A, const cv::Mat& B, Direction position,
                                        double recouvrement, double lissage) {
    const cv::Rect ra = bande(A.size(), position, recouvrement);
    const cv::Rect rb = bande(B.size(), oppose(position), recouvrement);
    if (ra.width < 2 || ra.height < 2) return std::nullopt;
    const cv::Mat a = enGris(A(ra)), b = enGris(B(rb));
    // Décalage si la mesure résiduelle est nulle
    const double ty0 = position == Direction::Bottom ? A.rows - a.rows : 0;
    const double tx0 = position == Direction::Right  ? A.cols - a.cols : 0;
    // Même découpe dans les deux bandes : le décalage résiduel mesuré n'en est pas modifié
    const auto zone = zoneAvecContenu(a, b);
    if (!zone) return std::nullopt;
    const Correlation c = correlationDePhase(a(*zone), b(*zone), lissage);
    return Decalage{ty0 + c.dy, tx0 + c.dx, rapportPics(c.lisse)};
}

struct Parametres {
    double lissage, seuilRapport, lissageLocal, ecartMax, espacementMin;
    int nbPoints, fenetre;
};

// Translation globale puis points raffinés localement, pour B en bas ou à droite de A
bool apparier(const cv::Mat& A, const cv::Mat& B, Direction position, double recouvrement,
              const Parametres& p, Matches& out) {
    const auto d = mesurerDecalage(A, B, position, recouvrement, p.lissage);
    if (!d || d->rapport < p.seuilRapport) return false;
    out.x = static_cast<float>(d->tx);
    out.y = static_cast<float>(d->ty);
    if (p.nbPoints == 0) return true;

    // Parties de A et de B qui se superposent quand B est placée en (ty, tx), arrondi au pixel
    const int ty = static_cast<int>(std::lround(d->ty)), tx = static_cast<int>(std::lround(d->tx));
    const int y0 = std::max(0, ty), y1 = std::min(A.rows, ty + B.rows);
    const int x0 = std::max(0, tx), x1 = std::min(A.cols, tx + B.cols);
    const int f = p.fenetre, marge = f / 2 + static_cast<int>(std::ceil(p.ecartMax)) + 1;
    if (y1 - y0 < f + 2 * marge || x1 - x0 < f + 2 * marge) return true;   // trop étroit pour des patchs
    const cv::Rect communA(x0, y0, x1 - x0, y1 - y0);
    const cv::Mat a = enGris(A(communA)), b = enGris(B(communA - cv::Point(tx, ty)));

    // Candidats : coins texturés et espacés, assez loin des bords pour que patch + correction restent dedans
    cv::Mat masque = valides(a);
    masque.rowRange(0, marge).setTo(0);
    masque.rowRange(masque.rows - marge, masque.rows).setTo(0);
    masque.colRange(0, marge).setTo(0);
    masque.colRange(masque.cols - marge, masque.cols).setTo(0);
    if (cv::countNonZero(masque) == 0) return true;
    cv::Mat image;
    sansNaN(a).convertTo(image, CV_32F);
    std::vector<cv::Point2f> candidats;
    cv::goodFeaturesToTrack(image, candidats, 3 * p.nbPoints, 0.01, p.espacementMin, masque);

    struct Point { cv::Point2f a, b; float confiance; };
    std::vector<Point> points;
    const int minValides = f * f / 2;
    for (const cv::Point2f& c : candidats) {
        const cv::Rect patch(static_cast<int>(c.x) - f / 2, static_cast<int>(c.y) - f / 2, f, f);
        const cv::Mat pa = a(patch), pb = b(patch);
        if (cv::countNonZero(valides(pa)) < minValides || cv::countNonZero(valides(pb)) < minValides) continue;

        // Résidu local par rapport à la translation arrondie : pa(y, x) ≈ pb(y - dy, x - dx)
        const Correlation local = correlationDePhase(pa, pb, p.lissageLocal);
        const double ly = ty + local.dy, lx = tx + local.dx;
        if (std::abs(ly - d->ty) > p.ecartMax || std::abs(lx - d->tx) > p.ecartMax) continue;
        if (rapportPics(local.lisse) < p.seuilRapport) continue;

        // Confiance : NCC entre le patch de A et celui de B recalé au pixel près
        const cv::Rect patchB = patch - cv::Point(static_cast<int>(std::lround(local.dx)),
                                                  static_cast<int>(std::lround(local.dy)));
        cv::Mat fa, fb, ncc;
        sansNaN(pa).convertTo(fa, CV_32F);
        sansNaN(b(patchB)).convertTo(fb, CV_32F);
        cv::matchTemplate(fa, fb, ncc, cv::TM_CCOEFF_NORMED);
        float confiance = ncc.at<float>(0, 0);
        confiance = std::isfinite(confiance) ? std::clamp(confiance, 0.f, 1.f) : 0.f;

        const cv::Point2f pA(c.x + x0, c.y + y0);
        points.push_back({pA, pA - cv::Point2f(static_cast<float>(lx), static_cast<float>(ly)), confiance});
    }

    std::sort(points.begin(), points.end(),
              [](const Point& u, const Point& v) { return u.confiance > v.confiance; });
    if (static_cast<int>(points.size()) > p.nbPoints) points.resize(p.nbPoints);
    for (const Point& pt : points) {
        out.pointsA.push_back(pt.a);
        out.pointsB.push_back(pt.b);
        out.confidence.push_back(pt.confiance);
    }
    return true;
}

} // namespace

CorrelationPhase::CorrelationPhase(const Json& params) : params(params) {}

bool CorrelationPhase::match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) {
    out = {};
    if (a.empty() || b.empty() || a.size() != b.size()) return false;
    if (ctx.overlap <= 0.f || ctx.overlap > 1.f) return false;

    auto param = [&](const char* nom, double defaut) {
        return params.is_object() ? params.value(nom, defaut) : defaut;
    };
    const Parametres p{param("lissage", 3.0), param("seuil_rapport", 1.5), param("lissage_local", 1.5),
                       param("ecart_max", 3.0), param("espacement_min", 16.0),
                       static_cast<int>(param("nb_points", 500)), static_cast<int>(param("fenetre", 64))};
    if (p.nbPoints < 0 || p.fenetre < 8) return false;

    if (ctx.dir == Direction::Bottom || ctx.dir == Direction::Right)
        return apparier(a, b, ctx.dir, ctx.overlap, p, out);

    // B en haut ou à gauche : on se ramène au cas bas / droite en échangeant A et B
    if (!apparier(b, a, oppose(ctx.dir), ctx.overlap, p, out)) return false;
    out.x = -out.x;
    out.y = -out.y;
    std::swap(out.pointsA, out.pointsB);
    return true;
}

} // namespace stitching::matching
