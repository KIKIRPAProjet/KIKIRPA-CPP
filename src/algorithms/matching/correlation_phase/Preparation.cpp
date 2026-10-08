#include "algorithms/matching/correlation_phase/Preparation.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace stitching::matching::correlation_phase {

// ======================================== Public ========================================

Parametres Preparation::lireParametres(const Json& params) {
    Parametres p;
    if (params.is_null()) return p;
    if (!params.is_object()) throw std::invalid_argument("phase-correlation : les paramètres doivent être un objet JSON");
    p.lissage       = params.value("lissage", p.lissage);
    p.seuilRapport  = params.value("seuil_rapport", p.seuilRapport);
    p.lissageLocal  = params.value("lissage_local", p.lissageLocal);
    p.ecartMax      = params.value("ecart_max", p.ecartMax);
    p.espacementMin = params.value("espacement_min", p.espacementMin);
    p.nbPoints      = params.value("nb_points", p.nbPoints);
    p.fenetre       = params.value("fenetre", p.fenetre);
    if (p.nbPoints < 0 || p.fenetre < 8 || p.lissage < 0 || p.lissageLocal < 0 || p.ecartMax < 0 || p.espacementMin < 0)
        throw std::invalid_argument("phase-correlation : paramètre invalide (nb_points >= 0, fenetre >= 8, "
                                    "lissage, lissage_local, ecart_max, espacement_min >= 0)");
    return p;
}

std::optional<PaireOrientee> Preparation::orienter(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx) {
    if (a.empty() || b.empty() || a.size() != b.size()) return std::nullopt;
    if (ctx.overlap <= 0.f || ctx.overlap > 1.f) return std::nullopt;

    // B en haut ou à gauche : on se ramène au cas bas / droite en échangeant A et B
    const bool inversee = ctx.dir == Direction::Top || ctx.dir == Direction::Left;
    PaireOrientee paire;
    paire.A        = inversee ? b : a;
    paire.B        = inversee ? a : b;
    paire.extA     = zoneExterieure(paire.A);
    paire.extB     = zoneExterieure(paire.B);
    paire.position = inversee ? oppose(ctx.dir) : ctx.dir;
    paire.inversee = inversee;
    return paire;
}

std::optional<Bandes> Preparation::bandes(const PaireOrientee& paire, double recouvrement) {
    // On corrèle seulement les deux bandes censées se recouvrir (recouvrement nominal du rail),
    // pas les tuiles entières : ailleurs, le contenu est différent et brouillerait le pic.
    const cv::Rect ra = bande(paire.A.size(), paire.position, recouvrement);
    const cv::Rect rb = bande(paire.B.size(), oppose(paire.position), recouvrement);
    if (ra.width < 2 || ra.height < 2) return std::nullopt;
    const cv::Mat a = enGris(paire.A(ra), paire.extA(ra));
    const cv::Mat b = enGris(paire.B(rb), paire.extB(rb));

    // Même découpe dans les deux bandes : le décalage résiduel mesuré n'en est pas modifié
    const auto zone = zoneAvecContenu(a, b);
    if (!zone) return std::nullopt;

    Bandes resultat;
    resultat.a = a(*zone);
    resultat.b = b(*zone);
    resultat.origine = paire.position == Direction::Bottom ? cv::Point2d(0, paire.A.rows - ra.height)
                                                           : cv::Point2d(paire.A.cols - ra.width, 0);
    return resultat;
}

std::optional<Recouvrement> Preparation::recouvrement(const PaireOrientee& paire, const Mesure& mesure,
                                                      const Parametres& p) {
    // Parties de A et de B qui se superposent quand B est placée à la translation globale arrondie
    const cv::Point decalage(static_cast<int>(std::lround(mesure.tx)), static_cast<int>(std::lround(mesure.ty)));
    const int y0 = std::max(0, decalage.y), y1 = std::min(paire.A.rows, decalage.y + paire.B.rows);
    const int x0 = std::max(0, decalage.x), x1 = std::min(paire.A.cols, decalage.x + paire.B.cols);

    // Un patch centré sur un candidat, puis décalé de la correction locale, doit rester dans la zone
    const int marge = p.fenetre / 2 + static_cast<int>(std::ceil(p.ecartMax)) + 1;
    if (y1 - y0 < p.fenetre + 2 * marge || x1 - x0 < p.fenetre + 2 * marge) return std::nullopt;

    const cv::Rect zoneA(x0, y0, x1 - x0, y1 - y0);
    const cv::Rect zoneB = zoneA - decalage;
    Recouvrement resultat;
    resultat.a        = enGris(paire.A(zoneA), paire.extA(zoneA));
    resultat.b        = enGris(paire.B(zoneB), paire.extB(zoneB));
    resultat.origineA = zoneA.tl();
    resultat.decalage = decalage;
    resultat.marge    = marge;
    return resultat;
}

cv::Mat Preparation::valides(const cv::Mat& image) {
    return image == image;   // NaN != NaN
}

cv::Mat Preparation::remplirVides(const cv::Mat& image) {
    const cv::Mat masque = valides(image);
    cv::Mat resultat = image.clone();
    resultat.setTo(cv::mean(image, masque)[0], ~masque);
    return resultat;
}

// ======================================== Private ========================================

Direction Preparation::oppose(Direction d) {
    switch (d) {
        case Direction::Top:    return Direction::Bottom;
        case Direction::Bottom: return Direction::Top;
        case Direction::Left:   return Direction::Right;
        case Direction::Right:  return Direction::Left;
    }
    return d;
}

cv::Rect Preparation::bande(cv::Size forme, Direction cote, double recouvrement) {
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

cv::Mat Preparation::zoneExterieure(const cv::Mat& tuile) {
    // Un pixel noir à l'intérieur de la peinture (ombre bouchée à 0) qui ne touche pas le bord reste une donnée.
    // Calculé sur la tuile entière : une zone extérieure peut entrer dans une bande par l'intérieur de la tuile.
    // Limite : les quatre bords servent de départ, y compris ceux partagés avec une tuile voisine (voir la doc).
    cv::Mat vide;
    cv::inRange(tuile, cv::Scalar::all(0), cv::Scalar::all(0), vide);   // 255 si tous les canaux valent 0
    const int h = vide.rows, w = vide.cols;

    // Remplissage (connexité 4) depuis chaque pixel vide du bord : les zones reliées au bord passent à 128
    for (int x = 0; x < w; ++x)
        for (int y : {0, h - 1})
            if (vide.at<uchar>(y, x) == 255)
                cv::floodFill(vide, cv::Point(x, y), cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
    for (int y = 0; y < h; ++y)
        for (int x : {0, w - 1})
            if (vide.at<uchar>(y, x) == 255)
                cv::floodFill(vide, cv::Point(x, y), cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
    return vide == 128;
}

cv::Mat Preparation::enGris(const cv::Mat& tuile, const cv::Mat& exterieur) {
    // Pas de normalisation par percentiles comme dans le Python : la corrélation de phase (phase seule)
    // et la NCC sont insensibles à une transformation affine de l'intensité.
    cv::Mat lum;
    tuile.convertTo(lum, CV_64F);                   // pas de mise à l'échelle : la dynamique 16 bits est conservée
    if (lum.channels() > 1) {                       // tuile couleur : moyenne des canaux
        cv::Mat gris;
        cv::transform(lum, gris, cv::Mat(1, lum.channels(), CV_64F, cv::Scalar(1.0 / lum.channels())));
        lum = gris;
    }
    lum.setTo(std::numeric_limits<double>::quiet_NaN(), exterieur);

    // Zone vide ou uniforme : aucune information de position
    double bas = 0, haut = 0;
    const cv::Mat masque = valides(lum);
    if (cv::countNonZero(masque) > 0) cv::minMaxLoc(lum, &bas, &haut, nullptr, nullptr, masque);
    if (haut == bas) lum.setTo(std::numeric_limits<double>::quiet_NaN());
    return lum;
}

std::optional<cv::Rect> Preparation::zoneAvecContenu(const cv::Mat& a, const cv::Mat& b) {
    // Le bord d'une zone vide est au même endroit dans les deux bandes : on le coupe,
    // sinon il crée un faux pic à décalage nul.
    cv::Mat plein;
    cv::Mat(valides(a) & valides(b)).convertTo(plein, CV_64F, 1.0 / 255);
    const int h = plein.rows, w = plein.cols;
    std::vector<int> colonnes, lignes;

    // 1. Colonnes presque vides
    cv::Mat moyenneColonnes;
    cv::reduce(plein, moyenneColonnes, 0, cv::REDUCE_AVG);
    for (int x = 0; x < w; ++x)
        if (moyenneColonnes.at<double>(0, x) > PART_MIN_COLONNE) colonnes.push_back(x);

    // 2. Lignes à moitié vides (sur les colonnes gardées)
    if (!colonnes.empty()) {
        for (int y = 0; y < h; ++y) {
            const double* ligne = plein.ptr<double>(y);
            double somme = 0;
            for (int x : colonnes) somme += ligne[x];
            if (somme / colonnes.size() > PART_MIN_LIGNE) lignes.push_back(y);
        }
    }

    // 3. Colonnes à moitié vides (sur les lignes gardées)
    if (!lignes.empty()) {
        std::vector<double> somme(w, 0.0);
        for (int y : lignes) {
            const double* ligne = plein.ptr<double>(y);
            for (int x = 0; x < w; ++x) somme[x] += ligne[x];
        }
        colonnes.clear();
        for (int x = 0; x < w; ++x)
            if (somme[x] / lignes.size() > PART_MIN_LIGNE) colonnes.push_back(x);
    }

    if (lignes.empty() || colonnes.empty()) return std::nullopt;
    if (lignes.back() - lignes.front() < COTE_MIN_CONTENU || colonnes.back() - colonnes.front() < COTE_MIN_CONTENU)
        return std::nullopt;
    return cv::Rect(colonnes.front(), lignes.front(),
                    colonnes.back() - colonnes.front() + 1, lignes.back() - lignes.front() + 1);
}

} // namespace stitching::matching::correlation_phase
