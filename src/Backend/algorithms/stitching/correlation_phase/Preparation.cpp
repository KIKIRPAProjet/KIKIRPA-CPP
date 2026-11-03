//Preparation.cpp
#include "algorithms/stitching/correlation_phase/Preparation.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace stitching::matching::correlation_phase {

void Preparation::verifierParametres(const Parametres& p) {
    // Vérifié une fois à la construction : une valeur hors domaine est une erreur de configuration,
    // pas un échec de mesure, d'où l'exception plutôt qu'un false dans match()
    if (p.nbPoints < 0 || p.fenetre < 8 || p.lissage < 0 || p.lissageLocal < 0 || p.ecartMax < 0 || p.espacementMin < 0
        || p.marge < 0 || p.fractionValideMin < 0 || p.fractionValideMin > 1 || p.facteurDft < 1)
        throw std::invalid_argument("phase-correlation : paramètre invalide (nbPoints >= 0, fenetre >= 8, "
                                    "facteurDft >= 1, fractionValideMin dans [0, 1], "
                                    "lissage, lissageLocal, ecartMax, espacementMin, marge >= 0)");
}

std::optional<PaireOrientee> Preparation::orienter(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx) {
    if (a.empty() || b.empty() || a.size() != b.size()) return std::nullopt;
    if (ctx.overlap <= 0.f || ctx.overlap > 1.f) return std::nullopt;

    // B en haut ou à gauche : on se ramène au cas bas / droite en échangeant A et B
    const bool inversee = ctx.dir == Direction::Top || ctx.dir == Direction::Left;
    PaireOrientee paire;
    // condition ? valeur si vrai : valeur si faux
    paire.A        = inversee ? b : a;
    paire.B        = inversee ? a : b;
    paire.extA     = zoneExterieure(paire.A);
    paire.extB     = zoneExterieure(paire.B);
    paire.position = inversee ? oppose(ctx.dir) : ctx.dir;
    paire.inversee = inversee;
    return paire;
}

cv::Point2d Preparation::orienterTranslation(const PaireOrientee& paire, cv::Point2d translation) {
    // Échanger A et B revient à placer A dans le repère de B : la translation change de signe
    return paire.inversee ? -translation : translation;
}

std::optional<Bandes> Preparation::bandes(const PaireOrientee& paire, double recouvrement, const Parametres& p) {
    // On corrèle seulement les deux bandes censées se recouvrir (recouvrement nominal du rail),
    // pas les tuiles entières : ailleurs, le contenu est différent et brouillerait le pic.
    const cv::Rect ra = bande(paire.A.size(), paire.position, recouvrement);
    const cv::Rect rb = bande(paire.B.size(), oppose(paire.position), recouvrement);
    const cv::Point2d origine = paire.position == Direction::Bottom ? cv::Point2d(0, paire.A.rows - ra.height)
                                                                    : cv::Point2d(paire.A.cols - ra.width, 0);
    // Les bandes sont prises au recouvrement nominal : le décalage résiduel attendu est nul,
    // et la marge du rail le borne directement
    return preparerBandes(paire, ra, rb, origine, margeRail(paire, p), p);
}

std::optional<Bandes> Preparation::zoneCommune(const PaireOrientee& paire, cv::Point2d translation,
                                               const Parametres& p) {
    // Les deux images montrent la même partie de la peinture, à l'erreur de la première passe près : tout leur
    // contenu se correspond. Rien n'est perdu quand le recouvrement réel dépasse le nominal, et aucun contenu
    // sans correspondance n'affaiblit le pic quand il est plus petit.
    const auto [ra, rb] = EstimationRecouvrement::zonesCommunes(paire.A.size(), paire.B.size(), translation);
    // Le décalage résiduel est mesuré autour de la translation arrondie : il est de l'ordre du pixel
    return preparerBandes(paire, ra, rb, cv::Point2d(ra.tl() - rb.tl()), MARGE_DEUXIEME_PASSE, p);
}

std::optional<Recouvrement> Preparation::recouvrement(const PaireOrientee& paire, const Mesure& mesure,
                                                      const Parametres& p) {
    // Parties de A et de B qui se superposent quand B est placée à la translation globale arrondie
    const auto [zoneA, zoneB] = EstimationRecouvrement::zonesCommunes(paire.A.size(), paire.B.size(),
                                                                      {mesure.tx, mesure.ty});

    // Un patch centré sur un candidat, puis décalé de la correction locale, doit rester dans la zone
    const int marge = p.fenetre / 2 + static_cast<int>(std::ceil(p.ecartMax)) + 1;
    if (zoneA.height < p.fenetre + 2 * marge || zoneA.width < p.fenetre + 2 * marge) return std::nullopt;

    Recouvrement resultat;
    resultat.a        = enGris(paire.A(zoneA), paire.extA(zoneA));
    resultat.b        = enGris(paire.B(zoneB), paire.extB(zoneB));
    resultat.origineA = zoneA.tl();
    resultat.decalage = zoneA.tl() - zoneB.tl();
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


Direction Preparation::oppose(Direction d) {
    switch (d) {
        case Direction::Top:    return Direction::Bottom;
        case Direction::Bottom: return Direction::Top;
        case Direction::Left:   return Direction::Right;
        case Direction::Right:  return Direction::Left;
    }
    return d;
}

std::optional<Bandes> Preparation::preparerBandes(const PaireOrientee& paire, cv::Rect ra, cv::Rect rb,
                                                  cv::Point2d origine, double marge, const Parametres& p) {
    if (ra.width < 2 || ra.height < 2) return std::nullopt;
    const cv::Mat a = enGris(paire.A(ra), paire.extA(ra));
    const cv::Mat b = enGris(paire.B(rb), paire.extB(rb));

    // Bandes surtout faites de fond ou de reflets : le peu qui reste ne suffit pas à une mesure fiable
    const double fractionValide = cv::countNonZero(cv::Mat(valides(a) & valides(b))) / static_cast<double>(a.total());
    if (fractionValide < p.fractionValideMin) return std::nullopt;

    // Même découpe dans les deux bandes : le décalage résiduel mesuré n'en est pas modifié
    const auto zone = zoneAvecContenu(a, b);
    if (!zone) return std::nullopt;

    Bandes resultat;
    resultat.a       = a(*zone);
    resultat.b       = b(*zone);
    resultat.origine = origine;
    resultat.marge   = marge;
    return resultat;
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

double Preparation::margeRail(const PaireOrientee& paire, const Parametres& p) {
    if (p.marge > 0) return p.marge;
    // L'imprécision du rail grandit avec le pas, donc avec la taille de la tuile dans le sens du déplacement.
    // Même valeur par défaut que la chaîne Python de référence (Stitching_Python).
    const int cote = paire.position == Direction::Bottom ? paire.A.rows : paire.A.cols;
    return std::max(MARGE_PART_TUILE * cote, MARGE_MIN);
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

    // Pixels saturés (reflets) : l'écrêtage efface la texture, et la tache claire n'est pas au même endroit
    // dans les deux tuiles ; elle attirerait le pic vers un faux décalage.
    lum.setTo(std::numeric_limits<double>::quiet_NaN(), lum >= PART_SATURATION * valeurMax(tuile.depth()));

    // La transition entre une zone retirée et la peinture (quelques px sombres ou très clairs, non retirés)
    // est au même endroit dans les deux tuiles : elle formerait un faux détail commun, de décalage nul.
    // On la rogne. Le bord de l'image n'est pas érodé (valeur de bord par défaut de cv::erode).
    cv::Mat masque = valides(lum);
    if (cv::countNonZero(masque) < static_cast<int>(masque.total())) {
        cv::Mat rogne;
        cv::erode(masque, rogne, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * EROSION + 1, 2 * EROSION + 1)));
        lum.setTo(std::numeric_limits<double>::quiet_NaN(), ~rogne);
        masque = rogne;
    }

    // Zone vide ou uniforme : aucune information de position
    double bas = 0, haut = 0;
    if (cv::countNonZero(masque) > 0) cv::minMaxLoc(lum, &bas, &haut, nullptr, nullptr, masque);
    if (haut == bas) lum.setTo(std::numeric_limits<double>::quiet_NaN());
    return lum;
}

double Preparation::valeurMax(int profondeur) {
    // Les tuiles flottantes sont supposées normalisées entre 0 et 1
    switch (profondeur) {
        case CV_8U:  return std::numeric_limits<uint8_t>::max();
        case CV_8S:  return std::numeric_limits<int8_t>::max();
        case CV_16U: return std::numeric_limits<uint16_t>::max();
        case CV_16S: return std::numeric_limits<int16_t>::max();
        case CV_32S: return std::numeric_limits<int32_t>::max();
        default:     return 1.0;
    }
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
