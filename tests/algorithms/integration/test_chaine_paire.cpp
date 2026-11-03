// test_chaine_paire.cpp
// Chaîne complète sur UNE paire de tuiles, en appelant directement les algorithmes de la branche
// (les steps du pipeline ne sont pas encore implémentés). Aucun code de la bibliothèque n'est modifié :
// ce fichier ne fait qu'enchaîner les appels.
//
//   tiles  →  alignments  →  positions  →  seams  →  mosaic
//   lecture   CorrelationPhase   coins entiers   seamlib (coût + DP)   hard-cut, pixels d'origine
//
// Les noms des étapes reprennent les clés des signatures des steps prévues par l'équipe logiciel
// (branche fe/pipeline-json-parser : "tiles", "alignments", "positions", "seams", "mosaic").
//
//   test_chaine_paire
//       Vérité exacte. Deux tuiles découpées à des positions ENTIÈRES connues dans une texture générée,
//       dans les 4 directions, en gris puis en couleur. Dans B, le tiers de l'overlap côté A est éclairci
//       (écart de densité) : la seamline doit l'éviter. La fusion gardant les pixels d'origine, la mosaïque
//       doit alors être identique pixel à pixel à la source.
//       Code de retour 0 si tous les tests passent.
//
//   test_chaine_paire --paire A.tif B.tif position [recouvrement] [methode] [dossier]
//       Paire réelle : rapport de chaque étape sur la console, et dans `dossier` (défaut : dossier courant) :
//         chaine_mosaique.tif  mosaïque, même type que les tuiles (16 bits), sans profil ICC
//         chaine_apercu.png    aperçu réduit en 8 bits (affichage seulement), seamline en rouge
//         chaine_cout.png      carte de coût de l'overlap, seamline en rouge
//       position : bas, haut, droite, gauche (où est B par rapport à A, comme test_correlation_phase).
//       methode  : DP (défaut), Dijkstra-8, DP+Dijkstra-local.

#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "algorithms/seamline/dp_dijkstra/cost_catalog.hpp"
#include "algorithms/seamline/dp_dijkstra/solver.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace stitching::tests {

using matching::CorrelationPhase;
using Horloge = std::chrono::steady_clock;

constexpr int    COTE_TUILE      = 1000;   // px : tuiles carrées découpées dans la source (vérité exacte)
constexpr int    COTE_SOURCE     = 3000;   // px : texture générée, assez grande pour B dans les 4 directions
constexpr int    COIN_A          = 1000;   // px : coin haut-gauche de A dans la source (x et y)
constexpr double RECOUVREMENT    = 0.2;
constexpr double GAIN_ECLAIRCI   = 1.15;   // écart de densité appliqué à B sur le tiers de l'overlap côté A
constexpr double TOLERANCE_ARRONDI = 0.5;  // px : la translation mesurée doit s'arrondir à la vérité entière
constexpr uchar  MARQUE_CHEMIN   = 255;    // cotePremier : pixel de la seamline
constexpr uchar  MARQUE_GAUCHE   = 128;    // cotePremier : pixel à gauche de la seamline
constexpr int    COTE_APERCU     = 2000;   // px : plus grand côté des PNG de contrôle
constexpr double PERCENTILE_BAS  = 1.0;    // étirement du contraste des PNG de contrôle
constexpr double PERCENTILE_HAUT = 99.0;

int nbEchecs = 0;

void verifier(bool condition, const std::string& message) {
    std::cout << (condition ? "  [ok]    " : "  [ECHEC] ") << message << '\n';
    if (!condition) ++nbEchecs;
}

std::string format(const char* modele, double a, double b = 0, double c = 0, double d = 0) {
    char tampon[256];
    std::snprintf(tampon, sizeof tampon, modele, a, b, c, d);
    return tampon;
}

double msDepuis(Horloge::time_point debut) {
    return std::chrono::duration<double, std::milli>(Horloge::now() - debut).count();
}

// ======================================== Données de la chaîne ========================================

// "alignments" : translation mesurée entre les deux tuiles
struct Alignement {
    bool ok = false;
    Matches matches;               // dx, dy : coin haut-gauche de B dans le repère de A (sous-pixel)
    std::string erreur;            // CorrelationPhase::raisonEchec() si ok = false
    double ms = 0;
};

// "positions" : coin haut-gauche de chaque tuile dans la mosaïque, arrondi au pixel (pas d'interpolation)
struct Positions {
    cv::Point a, b;
    cv::Size taille;               // taille de la mosaïque
};

// "seams" : seamline dans l'overlap et répartition de ses pixels entre A et B
struct Couture {
    bool ok = false;
    std::string erreur;
    cv::Rect overlap;              // dans la mosaïque
    bool transposee = false;       // paire haut/bas : le solveur travaille sur l'overlap transposé
    seamlib::SolveResult solveur;  // chemin dans le repère du solveur
    std::vector<cv::Point> chemin; // chemin dans le repère de l'overlap (non transposé)
    cv::Mat garderA;               // CV_8U, taille de l'overlap : 255 = pixel pris dans A, 0 = dans B
    cv::Mat cout;                  // carte de coût traitée, repère de l'overlap (CV_32F)
    double msCout = 0;
};

// ======================================== Étapes ========================================

const std::map<std::string, Direction>& directions() {
    static const std::map<std::string, Direction> d = {
        {"bas", Direction::Bottom}, {"haut", Direction::Top}, {"droite", Direction::Right}, {"gauche", Direction::Left}};
    return d;
}

// alignments : corrélation de phase avec ses paramètres par défaut (comme la Factory)
Alignement aligner(const cv::Mat& A, const cv::Mat& B, Direction position, double recouvrement) {
    Alignement al;
    CorrelationPhase cp;
    const auto debut = Horloge::now();
    al.ok = cp.match(A, B, {position, static_cast<float>(recouvrement)}, al.matches);
    al.ms = msDepuis(debut);
    al.erreur = cp.raisonEchec();
    return al;
}

// positions : A en (0, 0) sauf si B dépasse en haut ou à gauche ; translation arrondie au pixel
Positions positionner(const Alignement& al, cv::Size tailleA, cv::Size tailleB) {
    const cv::Point b(static_cast<int>(std::lround(al.matches.dx)), static_cast<int>(std::lround(al.matches.dy)));
    Positions p;
    p.a = cv::Point(std::max(0, -b.x), std::max(0, -b.y));
    p.b = p.a + b;
    p.taille = cv::Size(std::max(p.a.x + tailleA.width, p.b.x + tailleB.width),
                        std::max(p.a.y + tailleA.height, p.b.y + tailleB.height));
    return p;
}

// seamlib attend du RGB (ou du gris) ; cv::imread donne du BGR ou du BGRA
cv::Mat versRgb(const cv::Mat& image) {
    if (image.channels() == 1) return image;
    cv::Mat rgb;
    cv::cvtColor(image, rgb, image.channels() == 4 ? cv::COLOR_BGRA2RGB : cv::COLOR_BGR2RGB);
    return rgb;
}

cv::Mat transposerSi(const cv::Mat& image, bool transposer) {
    if (!transposer) return image;
    cv::Mat t;
    cv::transpose(image, t);
    return t;
}

// Pixels à gauche de la seamline, seamline comprise (255), dans le repère du solveur.
// Le chemin est 8-connexe et va du bord haut au bord bas : un remplissage 4-connexe parti du bord
// gauche ne peut pas le traverser, même là où il fait des pas latéraux (Dijkstra).
cv::Mat cotePremier(const std::vector<cv::Point>& chemin, cv::Size taille) {
    cv::Mat marque = cv::Mat::zeros(taille, CV_8U);
    for (const cv::Point& p : chemin) marque.at<uchar>(p) = MARQUE_CHEMIN;
    for (int y = 0; y < taille.height; ++y)
        if (marque.at<uchar>(y, 0) == 0)
            cv::floodFill(marque, cv::Point(0, y), cv::Scalar(MARQUE_GAUCHE), nullptr, cv::Scalar(), cv::Scalar(), 4);
    return marque != 0;
}

// seams : carte de coût de l'overlap puis chemin de coût minimal, haut → bas dans le repère du solveur.
// Seamline verticale pour une paire gauche/droite ; paire haut/bas : overlaps transposés (convention seamlib).
Couture trouverCouture(const cv::Mat& A, const cv::Mat& B, const Positions& pos, Direction position,
                       seamlib::Method methode) {
    Couture c;
    c.overlap = cv::Rect(pos.a, A.size()) & cv::Rect(pos.b, B.size());
    if (c.overlap.empty()) {
        c.erreur = "pas d'overlap à la position mesurée";
        return c;
    }
    c.transposee = position == Direction::Top || position == Direction::Bottom;

    // Côté gauche du solveur : la tuile de gauche, ou celle du haut une fois transposée
    const bool premierEstA = c.transposee ? pos.a.y <= pos.b.y : pos.a.x <= pos.b.x;
    const cv::Mat cropA = versRgb(A(c.overlap - pos.a)), cropB = versRgb(B(c.overlap - pos.b));
    const cv::Mat premier = transposerSi(premierEstA ? cropA : cropB, c.transposee);
    const cv::Mat second  = transposerSi(premierEstA ? cropB : cropA, c.transposee);

    seamlib::CostMaps cartes;
    try {
        const auto debut = Horloge::now();
        cartes = seamlib::compute_cost(premier, second, seamlib::CostFunction::ColorGradient);
        c.msCout = msDepuis(debut);
        c.solveur = seamlib::solve_edge(cartes.treated, methode);
    } catch (const std::exception& e) {
        c.erreur = std::string("exception de seamlib : ") + e.what();
        return c;
    }
    const std::vector<cv::Point>& chemin = c.solveur.path;
    if (chemin.empty() || chemin.front().y != 0 || chemin.back().y != cartes.treated.rows - 1) {
        c.erreur = "la seamline ne va pas du bord haut au bord bas de l'overlap";
        return c;
    }

    const cv::Mat gauche = transposerSi(cotePremier(chemin, cartes.treated.size()), c.transposee);
    c.garderA = premierEstA ? gauche : ~gauche;
    c.cout = transposerSi(cartes.treated, c.transposee);
    for (const cv::Point& p : chemin) c.chemin.push_back(c.transposee ? cv::Point(p.y, p.x) : p);
    c.ok = true;
    return c;
}

// Pixels dont tous les canaux valent 0 : hors de l'œuvre (règle simple de coller() en Python)
cv::Mat nonVide(const cv::Mat& tuile) {
    cv::Mat vide;
    cv::inRange(tuile, cv::Scalar::all(0), cv::Scalar::all(0), vide);
    return ~vide;
}

// mosaic : hard-cut, pixels d'origine. Dans l'overlap, chaque pixel vient de la tuile choisie par la seamline,
// sauf si elle y est vide : on prend alors l'autre (la seamline ne connaît pas les pixels vides).
cv::Mat fusionner(const cv::Mat& A, const cv::Mat& B, const Positions& pos, const Couture& c) {
    cv::Mat mosaique = cv::Mat::zeros(pos.taille, A.type());
    const cv::Mat pleinA = nonVide(A);
    A.copyTo(mosaique(cv::Rect(pos.a, A.size())), pleinA);

    cv::Mat masqueB = nonVide(B);
    const cv::Mat garderA = c.garderA & pleinA(c.overlap - pos.a);
    masqueB(c.overlap - pos.b).setTo(0, garderA);
    B.copyTo(mosaique(cv::Rect(pos.b, B.size())), masqueB);
    return mosaique;
}

// ======================================== Contrôles ========================================

cv::Mat gris(const cv::Mat& image) {
    cv::Mat f;
    image.convertTo(f, CV_64F);
    if (f.channels() == 1) return f;
    cv::Mat g;
    cv::transform(f, g, cv::Mat(1, f.channels(), CV_64F, cv::Scalar(1.0 / f.channels())));
    return g;
}

// Écart |A − B| moyen (gris, unités brutes) sur la seamline et sur tout l'overlap.
// Une seamline utile passe là où les deux tuiles se ressemblent : le premier doit être bien plus petit.
std::pair<double, double> ecartsCouture(const cv::Mat& A, const cv::Mat& B, const Positions& pos, const Couture& c) {
    cv::Mat ecart;
    cv::absdiff(gris(A(c.overlap - pos.a)), gris(B(c.overlap - pos.b)), ecart);
    double surChemin = 0;
    for (const cv::Point& p : c.chemin) surChemin += ecart.at<double>(p);
    return {surChemin / c.chemin.size(), cv::mean(ecart)[0]};
}

std::string decrireType(const cv::Mat& m) {
    const int bits = m.depth() == CV_8U ? 8 : m.depth() == CV_16U ? 16 : m.depth() == CV_32F ? 32 : 64;
    return std::to_string(m.cols) + " x " + std::to_string(m.rows) + ", " + std::to_string(m.channels()) +
           " canal(aux), " + std::to_string(bits) + " bits";
}

// ======================================== Vérité exacte ========================================

// Fausse peinture 16 bits sans pixel à 0 : bruit à plusieurs échelles (même générateur que test_correlation_phase)
cv::Mat textureGeneree(cv::Size taille, unsigned graine) {
    cv::RNG rng(graine);
    cv::Mat somme = cv::Mat::zeros(taille, CV_64F);
    for (int echelle = 1; echelle <= 128; echelle *= 2) {
        cv::Mat bruit(std::max(2, taille.height / echelle), std::max(2, taille.width / echelle), CV_64F);
        rng.fill(bruit, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::Mat agrandi;
        cv::resize(bruit, agrandi, taille, 0, 0, cv::INTER_CUBIC);
        somme += agrandi * std::sqrt(static_cast<double>(echelle));
    }
    cv::normalize(somme, somme, 3000, 60000, cv::NORM_MINMAX);
    cv::Mat resultat;
    somme.convertTo(resultat, CV_16U);
    return resultat;
}

cv::Mat textureCouleur(cv::Size taille) {
    cv::Mat couleur;
    cv::merge(std::vector<cv::Mat>{textureGeneree(taille, 11), textureGeneree(taille, 12), textureGeneree(taille, 13)},
              couleur);
    return couleur;
}

// Tiers de l'overlap situé du côté de A (là où la seamline ne doit pas passer si B y est faussée)
cv::Rect tiersCoteA(cv::Rect ov, Direction position) {
    switch (position) {
        case Direction::Right:  return {ov.x, ov.y, ov.width / 3, ov.height};
        case Direction::Left:   return {ov.x + ov.width - ov.width / 3, ov.y, ov.width / 3, ov.height};
        case Direction::Bottom: return {ov.x, ov.y, ov.width, ov.height / 3};
        case Direction::Top:    return {ov.x, ov.y + ov.height - ov.height / 3, ov.width, ov.height / 3};
    }
    return {};
}

void testerVerite(const std::string& nom, const cv::Mat& source, seamlib::Method methode) {
    std::cout << "=== " << nom << " (" << seamlib::method_name(methode) << ") ===\n";
    const std::vector<std::pair<std::string, cv::Point>> cas = {
        {"droite", {778, -9}}, {"bas", {7, 812}}, {"gauche", {-831, 3}}, {"haut", {-5, -783}}};
    const cv::Point coinA(COIN_A, COIN_A);

    for (const auto& [nomPosition, vrai] : cas) {
        const Direction position = directions().at(nomPosition);
        const cv::Rect rectA(coinA, cv::Size(COTE_TUILE, COTE_TUILE)), rectB = rectA + vrai;
        const cv::Mat A = source(rectA).clone();
        cv::Mat B = source(rectB).clone();

        // Écart de densité dans B seulement, sur le tiers de l'overlap côté A
        const cv::Rect zone = tiersCoteA(rectA & rectB, position);
        cv::Mat eclairci;
        B(zone - rectB.tl()).convertTo(eclairci, -1, GAIN_ECLAIRCI);
        eclairci.copyTo(B(zone - rectB.tl()));

        std::cout << nomPosition << "  (vérité x = " << vrai.x << ", y = " << vrai.y << ")\n";
        const Alignement al = aligner(A, B, position, RECOUVREMENT);
        verifier(al.ok && std::abs(al.matches.dx - vrai.x) < TOLERANCE_ARRONDI &&
                     std::abs(al.matches.dy - vrai.y) < TOLERANCE_ARRONDI,
                 format("alignments : dx = %.3f, dy = %.3f (%.0f ms)", al.matches.dx, al.matches.dy, al.ms) +
                     (al.ok ? "" : " : " + al.erreur));
        if (!al.ok) continue;

        const Positions pos = positionner(al, A.size(), B.size());
        const Couture c = trouverCouture(A, B, pos, position, methode);
        verifier(c.ok, "seams : seamline de bord à bord de l'overlap" + (c.ok ? "" : " (" + c.erreur + ")"));
        if (!c.ok) continue;

        // La mosaïque commence au coin haut-gauche de l'union des deux tuiles dans la source
        const cv::Point origine(std::min(rectA.x, rectB.x), std::min(rectA.y, rectB.y));
        const cv::Rect zoneOverlap = zone - origine - c.overlap.tl();
        const bool evite = (zoneOverlap & cv::Rect(cv::Point(), c.garderA.size())) == zoneOverlap &&
                           cv::countNonZero(c.garderA(zoneOverlap)) == zoneOverlap.area();
        verifier(evite, "seams : la seamline évite la zone éclaircie" +
                            format(" (solveur %.1f ms)", c.solveur.wall_s * 1000));

        const cv::Mat mosaique = fusionner(A, B, pos, c);
        const cv::Rect couvert(origine, mosaique.size());
        cv::Mat masque = cv::Mat::zeros(mosaique.size(), CV_8U);
        masque(rectA - origine).setTo(255);
        masque(rectB - origine).setTo(255);
        const bool memeTaille = couvert == (rectA | rectB);
        const double ecartMax = memeTaille ? cv::norm(mosaique, source(couvert), cv::NORM_INF, masque) : -1;
        verifier(memeTaille && ecartMax == 0,
                 format("mosaic : %.0f x %.0f, identique à la source pixel à pixel (écart max %.0f)",
                        mosaique.cols, mosaique.rows, ecartMax));
    }
}

// ======================================== Paire réelle ========================================

// Image affichable (8 bits, BGR, réduite à COTE_APERCU px), contraste étiré entre deux percentiles des pixels non nuls
cv::Mat versAffichage(const cv::Mat& image, double& facteur) {
    facteur = std::min(1.0, static_cast<double>(COTE_APERCU) / std::max(image.rows, image.cols));
    cv::Mat petit, f;
    cv::resize(image, petit, cv::Size(), facteur, facteur, cv::INTER_AREA);
    if (petit.channels() == 4) cv::cvtColor(petit, petit, cv::COLOR_BGRA2BGR);
    petit.convertTo(f, CV_32F);

    std::vector<float> valeurs;
    const cv::Mat plat = f.reshape(1, 1);
    for (int i = 0; i < plat.cols; ++i)
        if (plat.at<float>(0, i) > 0) valeurs.push_back(plat.at<float>(0, i));
    double bas = 0, haut = 1;
    if (!valeurs.empty()) {
        const auto rangBas  = valeurs.begin() + static_cast<long>(PERCENTILE_BAS / 100 * (valeurs.size() - 1));
        const auto rangHaut = valeurs.begin() + static_cast<long>(PERCENTILE_HAUT / 100 * (valeurs.size() - 1));
        std::nth_element(valeurs.begin(), rangBas, valeurs.end());
        bas = *rangBas;
        std::nth_element(valeurs.begin(), rangHaut, valeurs.end());
        haut = std::max(static_cast<double>(*rangHaut), bas + 1e-6);
    }
    cv::Mat huit;
    f.convertTo(huit, CV_8U, 255.0 / (haut - bas), -bas * 255.0 / (haut - bas));
    if (huit.channels() == 1) cv::cvtColor(huit, huit, cv::COLOR_GRAY2BGR);
    return huit;
}

void dessinerChemin(cv::Mat& image, const std::vector<cv::Point>& chemin, cv::Point decalage, double facteur) {
    std::vector<cv::Point> points;
    for (const cv::Point& p : chemin)
        points.emplace_back(static_cast<int>((p.x + decalage.x) * facteur), static_cast<int>((p.y + decalage.y) * facteur));
    cv::polylines(image, points, false, cv::Scalar(0, 0, 255), 2);
}

int chainePaire(const std::string& cheminA, const std::string& cheminB, const std::string& nomPosition,
                double recouvrement, const std::string& nomMethode, const std::filesystem::path& dossier) {
    if (!directions().count(nomPosition)) {
        std::cerr << "position inconnue : " << nomPosition << " (bas, haut, droite, gauche)\n";
        return 2;
    }
    const Direction position = directions().at(nomPosition);
    seamlib::Method methode;
    try {
        methode = seamlib::method_from_name(nomMethode);
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (DP, Dijkstra-8, DP+Dijkstra-local)\n";
        return 2;
    }
    const auto debutChaine = Horloge::now();

    // tiles
    const cv::Mat A = cv::imread(cheminA, cv::IMREAD_UNCHANGED), B = cv::imread(cheminB, cv::IMREAD_UNCHANGED);
    if (A.empty() || B.empty()) {
        std::cerr << "lecture impossible : " << (A.empty() ? cheminA : cheminB) << '\n';
        return 2;
    }
    std::cout << "1. tiles\n   A : " << decrireType(A) << "\n   B : " << decrireType(B) << '\n';
    if (A.type() != B.type()) {
        std::cout << "   ECHEC : les deux tuiles n'ont pas le même type\n";
        return 1;
    }

    // alignments
    const Alignement al = aligner(A, B, position, recouvrement);
    std::cout << "2. alignments (CorrelationPhase, B " << nomPosition << ", recouvrement nominal " << recouvrement << ")\n";
    if (!al.ok) {
        std::cout << "   ECHEC : " << al.erreur << format(" (%.0f ms)\n", al.ms);
        return 1;
    }
    std::vector<float> confiances = al.matches.scores;
    std::sort(confiances.begin(), confiances.end());
    std::cout << format("   B dans A : dx = %.3f, dy = %.3f px  (%.0f ms)\n", al.matches.dx, al.matches.dy, al.ms)
              << "   " << al.matches.pointsA.size() << " points locaux"
              << (confiances.empty() ? "" : format(", confiance médiane %.3f", confiances[confiances.size() / 2])) << '\n';

    // positions
    const Positions pos = positionner(al, A.size(), B.size());
    std::cout << "3. positions (arrondies au pixel)\n"
              << format("   A en (%.0f, %.0f), B en (%.0f, %.0f), ", pos.a.x, pos.a.y, pos.b.x, pos.b.y)
              << format("mosaïque %.0f x %.0f\n", pos.taille.width, pos.taille.height);

    // seams
    const Couture c = trouverCouture(A, B, pos, position, methode);
    std::cout << "4. seams (seamlib, color_gradient, " << seamlib::method_name(methode) << ")\n"
              << format("   overlap %.0f x %.0f px en (%.0f, %.0f)", c.overlap.width, c.overlap.height,
                        c.overlap.x, c.overlap.y)
              << (c.transposee ? ", transposé pour le solveur" : "") << '\n';
    if (!c.ok) {
        std::cout << "   ECHEC : " << c.erreur << '\n';
        return 1;
    }
    const auto [ecartChemin, ecartOverlap] = ecartsCouture(A, B, pos, c);
    int bordMin = c.transposee ? c.overlap.height : c.overlap.width, bordMax = 0;
    for (const cv::Point& p : c.chemin) {
        const int travers = c.transposee ? p.y : p.x;   // position à travers l'overlap
        bordMin = std::min(bordMin, travers);
        bordMax = std::max(bordMax, travers);
    }
    std::cout << format("   carte de coût %.0f ms, solveur %.0f ms, mémoire estimée %.0f Mo\n", c.msCout,
                        c.solveur.wall_s * 1000, c.solveur.mem_est_mb)
              << format("   seamline : %.0f points, objectif %.2f, entre %.0f et %.0f px à travers l'overlap\n",
                        static_cast<double>(c.chemin.size()), c.solveur.objective, bordMin, bordMax)
              << format("   |A - B| moyen : %.1f sur la seamline, %.1f sur tout l'overlap (unités brutes)\n",
                        ecartChemin, ecartOverlap);

    // mosaic
    const auto debutFusion = Horloge::now();
    const cv::Mat mosaique = fusionner(A, B, pos, c);
    std::cout << "5. mosaic (hard-cut, pixels d'origine)\n"
              << format("   %.0f x %.0f px (%.0f ms)\n", mosaique.cols, mosaique.rows, msDepuis(debutFusion));

    std::filesystem::create_directories(dossier);
    const std::string fichierMosaique = (dossier / "chaine_mosaique.tif").string();
    const std::string fichierApercu = (dossier / "chaine_apercu.png").string();
    const std::string fichierCout = (dossier / "chaine_cout.png").string();
    double facteur = 1;
    cv::Mat apercu = versAffichage(mosaique, facteur);
    dessinerChemin(apercu, c.chemin, c.overlap.tl(), facteur);
    cv::Mat cout = versAffichage(c.cout, facteur);
    dessinerChemin(cout, c.chemin, cv::Point(), facteur);
    const bool ecrit = cv::imwrite(fichierMosaique, mosaique) && cv::imwrite(fichierApercu, apercu) &&
                       cv::imwrite(fichierCout, cout);
    std::cout << (ecrit ? "   " : "   ECHEC d'écriture : ") << fichierMosaique << ", " << fichierApercu << ", "
              << fichierCout << '\n';

    std::cout << "\nChaîne complète " << (ecrit ? "OK" : "KO")
              << format(" (%.1f s)\n", msDepuis(debutChaine) / 1000);
    return ecrit ? 0 : 1;
}

} // namespace stitching::tests

int main(int argc, char** argv) {
    using namespace stitching::tests;
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--paire") {
        if (args.size() < 4) {
            std::cerr << "usage : test_chaine_paire --paire A.tif B.tif position [recouvrement] [methode] [dossier]\n";
            return 2;
        }
        return chainePaire(args[1], args[2], args[3], args.size() > 4 ? std::stod(args[4]) : RECOUVREMENT,
                           args.size() > 5 ? args[5] : "DP", args.size() > 6 ? args[6] : ".");
    }

    const cv::Size taille(COTE_SOURCE, COTE_SOURCE);
    testerVerite("texture générée, gris 16 bits", textureGeneree(taille, 1), seamlib::Method::DP);
    testerVerite("texture générée, couleur 16 bits", textureCouleur(taille), seamlib::Method::DP);

    std::cout << '\n' << (nbEchecs == 0 ? "Tous les tests passent." : std::to_string(nbEchecs) + " échec(s).") << '\n';
    return nbEchecs == 0 ? 0 : 1;
}
