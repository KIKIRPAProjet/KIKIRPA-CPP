// etude_confiance.cpp
// Étude de l'indicateur de confiance de CorrelationPhase sur les fonds sombres et les motifs répétitifs
// (résultats et conclusions : docs/algorithms/stitching/correlation_phase/EtudeConfiance.md).
//
//   etude_confiance [fichier.csv]
//       Génère des paires de tuiles à vérité exacte (graines fixes : même sortie à chaque exécution), 8 familles
//       de cas × 3 tirages (bruit capteur et résidu différents), les mesure, puis affiche une ligne par mesure
//       (erreur, indicateurs, décision actuelle) et la comparaison de plusieurs règles de décision.
//       Avec un fichier : écrit aussi le détail en CSV. Durée : ~35 s en Release.
//
// Indicateurs relevés pour chaque paire :
//   rapport  pic / second pic de la première passe (seul critère actuel, seuil 1,5)
//   ncc      NCC médiane des points locaux (Matches::confidenceScores)
//   disp     écart médian (px) entre pointsA − pointsB et la translation globale
//   autosim  auto-similarité de la bande de A (PhaseCorrelationStepClass::selfSimilarity)
//
// La ligne PIPELINE de la comparaison est la décision réelle du step (match + checkConfidence).

#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include "steps/phase_correlation_step.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <optional>
#include <iostream>
#include <string>
#include <vector>

namespace stitching::etude {

using matching::CorrelationPhase;
using matching::EstimationRecouvrement;

constexpr int    COTE          = 1000;   // px : tuiles carrées
constexpr float  RECOUVREMENT  = 0.2f;
constexpr int    PAS           = 800;    // px : position nominale de B sous A (COTE × (1 − RECOUVREMENT))
constexpr int    MARGE_FOURIER = 200;
constexpr double BRUIT         = 150;    // écart-type du bruit capteur, indépendant dans A et B (16 bits)
constexpr double ERREUR_MAX    = 0.5;    // px : au-delà, la mesure est fausse
constexpr int    NB_TIRAGES    = 3;      // tirages par cas : bruit et résidu différents
const cv::Point COIN_A(300, 300);

// ---------------------------------------------------------------- génération

cv::Mat decalerFourier(const cv::Mat& image, cv::Point2d f) {
    cv::Mat canal, spectre, decale;
    image.convertTo(canal, CV_64F);
    const int h = image.rows, w = image.cols;
    cv::Mat rampe(h, w, CV_64FC2);
    for (int v = 0; v < h; ++v)
        for (int u = 0; u < w; ++u) {
            const double fu = (u <= w / 2 ? u : u - w) / static_cast<double>(w);
            const double fv = (v <= h / 2 ? v : v - h) / static_cast<double>(h);
            const double phase = 2 * CV_PI * (fu * f.x + fv * f.y);
            rampe.at<cv::Vec2d>(v, u) = {std::cos(phase), std::sin(phase)};
        }
    cv::dft(canal, spectre, cv::DFT_COMPLEX_OUTPUT);
    cv::mulSpectrums(spectre, rampe, spectre, 0);
    cv::idft(spectre, decale, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    return decale;   // CV_64F
}

// Bruit à plusieurs échelles, normalisé dans [0, 1]
cv::Mat texture(cv::Size taille, unsigned graine) {
    cv::RNG rng(graine);
    cv::Mat somme = cv::Mat::zeros(taille, CV_64F);
    for (int echelle = 1; echelle <= 128; echelle *= 2) {
        cv::Mat bruit(std::max(2, taille.height / echelle), std::max(2, taille.width / echelle), CV_64F);
        rng.fill(bruit, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::Mat agrandi;
        cv::resize(bruit, agrandi, taille, 0, 0, cv::INTER_CUBIC);
        somme += agrandi * std::sqrt(static_cast<double>(echelle));
    }
    cv::normalize(somme, somme, 0, 1, cv::NORM_MINMAX);
    return somme;
}

// Motif de période (px, py) répété (0 = pas de répétition sur cet axe), dans [0, 1]
cv::Mat motif(cv::Size taille, int px, int py, unsigned graine) {
    const cv::Mat base = texture({px > 0 ? px : taille.width, py > 0 ? py : taille.height}, graine);
    cv::Mat r;
    cv::repeat(base, taille.height / base.rows + 1, taille.width / base.cols + 1, r);
    return r(cv::Rect({0, 0}, taille)).clone();
}

// Scène (CV_64F, en niveaux 16 bits) + masque des pixels « hors de l'œuvre » (255 = noir exact)
struct Scene {
    cv::Mat image;
    cv::Mat exterieur;
};

const cv::Size TAILLE_SCENE(COTE + 2 * COIN_A.x + 200, COTE + PAS + 2 * COIN_A.y + 200);

// Paire A, B (16 bits) : A au coin COIN_A, B à COIN_A + (vrai.x, PAS + vrai.y). Bruit capteur indépendant.
std::pair<cv::Mat, cv::Mat> paire(const Scene& s, cv::Point2d vrai, unsigned graine) {
    const cv::Point2d coinB = cv::Point2d(COIN_A) + cv::Point2d(vrai.x, PAS + vrai.y);
    const cv::Point entier(static_cast<int>(std::floor(coinB.x)), static_cast<int>(std::floor(coinB.y)));
    const cv::Rect rA(COIN_A, cv::Size(COTE, COTE)), rB(entier, cv::Size(COTE, COTE));
    const cv::Rect zone = cv::Rect(rB.tl() - cv::Point(MARGE_FOURIER, MARGE_FOURIER),
                                   rB.size() + cv::Size(2 * MARGE_FOURIER, 2 * MARGE_FOURIER))
                          & cv::Rect({0, 0}, s.image.size());
    const cv::Point2d frac = coinB - cv::Point2d(entier);
    cv::Mat a = s.image(rA).clone();
    cv::Mat b = decalerFourier(s.image(zone), frac)(rB - zone.tl()).clone();

    cv::RNG rng(graine);
    for (cv::Mat* m : {&a, &b}) {
        cv::Mat n(m->size(), CV_64F);
        rng.fill(n, cv::RNG::NORMAL, 0, BRUIT);
        *m += n;
    }
    cv::Mat A16, B16;
    a.convertTo(A16, CV_16U);
    b.convertTo(B16, CV_16U);
    // Noir exact hors de l'œuvre (après le bruit : un scan met ces pixels à 0), au pixel près pour B
    if (!s.exterieur.empty()) {
        A16.setTo(0, s.exterieur(rA));
        const cv::Rect rBarrondi(cv::Point(static_cast<int>(std::lround(coinB.x)), static_cast<int>(std::lround(coinB.y))),
                                 cv::Size(COTE, COTE));
        B16.setTo(0, s.exterieur(rBarrondi));
    }
    return {A16, B16};
}

// ---------------------------------------------------------------- mesure

struct Resultat {
    std::string famille, cas;
    cv::Point2d vrai;
    bool accepte = false;        // décision actuelle (paramètres par défaut, seuil 1,5)
    std::string raison;
    bool mesure = false;         // une translation a pu être calculée (seuil désactivé)
    cv::Point2d t;               // translation mesurée, seuil désactivé
    double erreur = NAN;         // px
    double rapport = NAN;        // pic / second pic (première passe)
    int nbPoints = 0;
    double ncc = NAN;            // NCC médiane des points
    double dispersion = NAN;     // px : écart médian des points à la translation globale
    double autoSim = NAN;        // auto-similarité de la bande de A (0 à 1)
    bool pipeline = false;       // acceptée par PhaseCorrelationStepClass (match + checkConfidence, réglages par défaut)
};


double mediane(std::vector<double> v) {
    if (v.empty()) return NAN;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

Resultat mesurer1(const std::string& famille, const std::string& cas, const Scene& s, cv::Point2d vrai, unsigned graine) {
    const auto [a, b] = paire(s, vrai, graine);
    const PairContext ctx{Direction::Bottom, RECOUVREMENT};
    const cv::Point2d attendu(vrai.x, PAS + vrai.y);
    Resultat r{famille, cas, vrai};
    r.autoSim = StepsNs::PhaseCorrelationStepClass::selfSimilarity(a, Direction::Bottom, RECOUVREMENT, 0);

    CorrelationPhase defaut;
    Matches m;
    r.accepte = defaut.match(a, b, ctx, m);
    r.raison = defaut.raisonEchec();
    if (r.accepte) {
        const auto v = StepsNs::PhaseCorrelationStepClass::checkConfidence(m, a, Direction::Bottom, RECOUVREMENT, {}, {});
        r.pipeline = v.reason.empty();
        if (!v.reason.empty()) r.raison = v.reason;
    }

    // Même mesure, seuil désactivé : ce que l'on obtiendrait en faisant confiance au pic
    matching::CorrelationPhase::Parametres sans;
    sans.seuilRapport = 0;
    EstimationRecouvrement premiere(sans);
    const auto reel = premiere.estimer(a, b, ctx);
    if (!reel) return r;
    r.rapport = reel->peakRatio;
    PairContext ctx2 = ctx;
    ctx2.estimatedTranslation = reel->translationBInA;
    CorrelationPhase seconde(sans);
    Matches m2;
    if (seconde.match(a, b, ctx2, m2)) {
        r.mesure = true;
        r.t = {m2.translationX, m2.translationY};
    } else {
        r.mesure = true;   // deuxième passe en échec : on garde la position entière de la première
        r.t = reel->translationBInA;
    }
    r.erreur = std::hypot(r.t.x - attendu.x, r.t.y - attendu.y);
    r.nbPoints = static_cast<int>(m2.pointsInA.size());
    std::vector<double> ncc, disp;
    for (size_t i = 0; i < m2.pointsInA.size(); ++i) {
        ncc.push_back(m2.confidenceScores[i]);
        const cv::Point2d d = cv::Point2d(m2.pointsInA[i] - m2.pointsInB[i]) - r.t;
        disp.push_back(std::hypot(d.x, d.y));
    }
    r.ncc = mediane(ncc);
    r.dispersion = mediane(disp);
    return r;
}

// Résidus (dans la marge du rail, ±50 px) des tirages 0, 1, 2 : l'alias d'un motif tombe à des endroits différents
const cv::Point2d RESIDUS[NB_TIRAGES] = {{3.37, 12.62}, {-8.71, -4.15}, {14.2, 27.9}};

// NB_TIRAGES mesures du même cas. `vrai` fixé (motif hors marge) ou, s'il est vide, pris dans RESIDUS.
void mesurer(std::vector<Resultat>& res, const std::string& famille, const std::string& cas, const Scene& s,
             unsigned graine, std::optional<cv::Point2d> vrai = std::nullopt) {
    for (int k = 0; k < NB_TIRAGES; ++k)
        res.push_back(mesurer1(famille, cas, s, vrai ? *vrai : RESIDUS[k], graine * 10 + k));
}

// ---------------------------------------------------------------- cas

std::vector<Resultat> etude() {
    std::vector<Resultat> res;
    auto niveaux = [](const cv::Mat& t, double bas, double haut) { return cv::Mat(t * (haut - bas) + bas); };

    // A. Référence
    for (unsigned g : {1u, 2u, 3u}) {
        Scene s{niveaux(texture(TAILLE_SCENE, g), 3000, 60000), {}};
        mesurer(res, "reference", "texture graine " + std::to_string(g), s, 100 + g);
    }

    // B. Fond noir (hors de l'œuvre, 0 exact) sur une part croissante de la bande, à gauche
    for (double part : {0.3, 0.5, 0.7, 0.75, 0.8, 0.9}) {
        Scene s{niveaux(texture(TAILLE_SCENE, 1), 3000, 60000), cv::Mat::zeros(TAILLE_SCENE, CV_8U)};
        s.exterieur.colRange(0, COIN_A.x + static_cast<int>(part * COTE)).setTo(255);
        mesurer(res, "fond_noir", "noir sur " + std::to_string(static_cast<int>(part * 100)) + " %", s, 200);
    }

    // C. Fond presque noir (non nul : pris pour de la peinture), bruit seul, sur une part croissante de la bande
    for (double part : {0.3, 0.5, 0.7, 0.8, 0.9, 0.95}) {
        cv::Mat img = niveaux(texture(TAILLE_SCENE, 1), 3000, 60000);
        img.colRange(0, COIN_A.x + static_cast<int>(part * COTE)).setTo(400);
        mesurer(res, "fond_presque_noir", "uni + bruit sur " + std::to_string(static_cast<int>(part * 100)) + " %",
                Scene{img, {}}, 300);
    }

    // D. Peinture sombre peu contrastée sur toute la tuile : amplitude de la texture / bruit capteur
    for (double amplitude : {3000.0, 600.0, 300.0, 150.0, 75.0, 40.0, 20.0}) {
        Scene s{niveaux(texture(TAILLE_SCENE, 1), 400, 400 + amplitude), {}};
        char cas[64];
        std::snprintf(cas, sizeof cas, "contraste/bruit = %.2f", amplitude / BRUIT);
        mesurer(res, "sombre_peu_contraste", cas, s, 400);
    }

    // E. Motif répétitif dans le sens du déplacement (période py), toute la tuile
    for (int py : {20, 30, 40, 60, 75, 120}) {
        Scene s{niveaux(motif(TAILLE_SCENE, 0, py, 7), 3000, 60000), {}};
        mesurer(res, "motif_vertical", "periode " + std::to_string(py) + " px", s, 500);
    }

    // F. Motif en grille (toile, carrelage) : période dans les deux axes
    for (int p : {20, 40, 75}) {
        Scene s{niveaux(motif(TAILLE_SCENE, p, p, 8), 3000, 60000), {}};
        mesurer(res, "motif_grille", "grille " + std::to_string(p) + " px", s, 600);
    }

    // G. Motif + texture faible (motif imparfait, comme une vraie toile peinte) : poids de la texture
    for (double w : {0.05, 0.15, 0.3}) {
        const cv::Mat m = motif(TAILLE_SCENE, 30, 30, 9), t = texture(TAILLE_SCENE, 2);
        Scene s{niveaux(m * (1 - w) + t * w, 3000, 60000), {}};
        char cas[64];
        std::snprintf(cas, sizeof cas, "grille 30 px + texture %.0f %%", w * 100);
        mesurer(res, "motif_imparfait", cas, s, 700);
    }

    // H. Motif de période > marge, vraie translation HORS de la marge : un alias tombe dans la marge
    for (double hors : {60.0, 70.0}) {
        Scene s{niveaux(motif(TAILLE_SCENE, 0, 75, 7), 3000, 60000), {}};
        char cas[64];
        std::snprintf(cas, sizeof cas, "periode 75, residu %.0f px (hors marge 50)", hors);
        mesurer(res, "motif_hors_marge", cas, s, 800, cv::Point2d(3.37, hors + 0.62));
    }
    // ... et la même translation sur une texture normale (référence : doit échouer, hors marge)
    {
        Scene s{niveaux(texture(TAILLE_SCENE, 1), 3000, 60000), {}};
        mesurer(res, "motif_hors_marge", "texture, residu 70 px (hors marge 50)", s, 801, cv::Point2d(3.37, 70.62));
    }
    return res;
}

bool juste(const Resultat& r) { return r.mesure && r.erreur <= ERREUR_MAX; }

std::string verdict(bool accepte, bool juste) {
    if (accepte) return juste ? "fiable" : "FAUX ACCEPTE";
    return juste ? "rejet inutile" : "rejet justifie";
}
std::string verdict(const Resultat& r) { return verdict(r.accepte, juste(r)); }

// Règles de décision comparées. « actuelle » = ce que fait match() aujourd'hui (rapport >= 1,5 + contrôles internes).
struct Regle {
    std::string nom;
    std::function<bool(const Resultat&)> accepte;
};
bool pointsCoherents(const Resultat& r, double nccMin, double dispMax) {
    return r.nbPoints > 0 && r.ncc >= nccMin && r.dispersion <= dispMax;
}
const std::vector<Regle> REGLES = {
    {"PIPELINE (PhaseCorrelationStep, defaut)", [](const Resultat& r) { return r.pipeline; }},
    {"actuelle (rapport >= 1.5)", [](const Resultat& r) { return r.accepte; }},
    {"rapport >= 2", [](const Resultat& r) { return r.accepte && r.rapport >= 2; }},
    {"rapport >= 3", [](const Resultat& r) { return r.accepte && r.rapport >= 3; }},
    {"actuelle + ncc >= 0.5", [](const Resultat& r) { return r.accepte && r.nbPoints > 0 && r.ncc >= 0.5; }},
    {"actuelle + dispersion <= 0.25", [](const Resultat& r) { return r.accepte && r.nbPoints > 0 && r.dispersion <= 0.25; }},
    {"actuelle + ncc >= 0.5 + disp <= 0.25", [](const Resultat& r) { return r.accepte && pointsCoherents(r, 0.5, 0.25); }},
    {"rapport >= 2 + ncc >= 0.5 + disp <= 0.25", [](const Resultat& r) { return r.accepte && r.rapport >= 2 && pointsCoherents(r, 0.5, 0.25); }},
    {"actuelle + ncc >= 0.3 + disp <= 0.5", [](const Resultat& r) { return r.accepte && pointsCoherents(r, 0.3, 0.5); }},
    {"actuelle + autosim < 0.8", [](const Resultat& r) { return r.accepte && !(r.autoSim >= 0.8); }},
    {"actuelle + autosim < 0.6", [](const Resultat& r) { return r.accepte && !(r.autoSim >= 0.6); }},
    {"actuelle + ncc>=0.3 + disp<=0.5 + autosim<0.8", [](const Resultat& r) { return r.accepte && pointsCoherents(r, 0.3, 0.5) && !(r.autoSim >= 0.8); }},
    {"actuelle + ncc>=0.3 + disp<=0.5 + autosim<0.6", [](const Resultat& r) { return r.accepte && pointsCoherents(r, 0.3, 0.5) && !(r.autoSim >= 0.6); }},
};

} // namespace stitching::etude

int main(int argc, char** argv) {
    using namespace stitching::etude;
    const auto res = etude();

    std::printf("%-22s %-40s %8s %7s %8s %6s %6s %7s  %s\n", "famille", "cas", "erreur", "rapport", "autosim",
                "points", "ncc", "disp", "decision actuelle (seuil 1.5)");
    for (const auto& r : res)
        std::printf("%-22s %-40s %8.3f %7.2f %8.2f %6d %6.3f %7.3f  %s\n", r.famille.c_str(), r.cas.c_str(), r.erreur,
                    r.rapport, r.autoSim, r.nbPoints, r.ncc, r.dispersion, verdict(r).c_str());

    std::printf("\nComparaison des regles (%zu mesures, juste = erreur <= %.1f px)\n", res.size(), ERREUR_MAX);
    std::printf("%-42s %7s %13s %15s %14s\n", "regle", "fiable", "FAUX ACCEPTE", "rejet justifie", "rejet inutile");
    for (const auto& regle : REGLES) {
        int n[4] = {};
        for (const auto& r : res) {
            const bool a = regle.accepte(r), j = juste(r);
            ++n[a ? (j ? 0 : 1) : (j ? 3 : 2)];
        }
        std::printf("%-42s %7d %13d %15d %14d\n", regle.nom.c_str(), n[0], n[1], n[2], n[3]);
    }

    if (argc > 1) {
        std::ofstream csv(argv[1]);
        csv << "famille,cas,vrai_dx,vrai_dy,accepte,mesure_dx,mesure_dy,erreur,rapport,nb_points,ncc_mediane,"
               "dispersion_mediane,autosimilarite,pipeline,verdict,raison\n";
        for (const auto& r : res) {
            std::string raison = r.raison;
            std::replace(raison.begin(), raison.end(), '"', '\'');
            csv << r.famille << ",\"" << r.cas << "\"," << r.vrai.x << ',' << PAS + r.vrai.y << ',' << r.accepte << ','
                << r.t.x << ',' << r.t.y << ',' << r.erreur << ',' << r.rapport << ',' << r.nbPoints << ',' << r.ncc
                << ',' << r.dispersion << ',' << r.autoSim << ',' << r.pipeline << ',' << verdict(r) << ",\"" << raison << "\"\n";
        }
    }
    return 0;
}
