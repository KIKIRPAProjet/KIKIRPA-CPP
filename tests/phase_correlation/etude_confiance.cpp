// etude_confiance.cpp
// Étude de l'indicateur de confiance de la corrélation de phase sur les fonds sombres et les motifs répétitifs
// (résultats et conclusions : docs/algorithms/stitching/phase_correlation/EtudeConfiance.md).
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

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"
#include "steps/phase_correlation_step.h"

#include <opencv2/core.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/cvdef.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace EtudeConfianceNs {

using DataNs::Direction;
using DataNs::Matches;
using DataNs::PairContext;
using CorrelationPhase = PhaseCorrelationNs::PhaseCorrelationMatcherClass;
using EstimationRecouvrement = OverlapEstimationNs::OverlapEstimatorClass;

constexpr int TILE_SIDE = 1000;   // px : tuiles carrées
constexpr float OVERLAP = 0.2f;
constexpr int NOMINAL_STEP = 800;   // px : position nominale de B sous A
constexpr int FOURIER_MARGIN = 200;
constexpr double SENSOR_NOISE = 150;   // écart-type du bruit capteur, indépendant dans A et B (16 bits)
constexpr double MAXIMUM_ERROR = 0.5;   // px : au-delà, la mesure est fausse
constexpr int DRAW_COUNT = 3;   // tirages par cas : bruit et résidu différents
constexpr int SCENE_PADDING = 200;
constexpr int REFERENCE_SEED_BASE = 100;
constexpr int DARK_BACKGROUND_VALUE = 400;
constexpr int IMAGE_MINIMUM = 3000;
constexpr int IMAGE_MAXIMUM = 60000;
constexpr int MASK_VALUE = 255;
constexpr int PERCENT = 100;
constexpr int IMPERFECT_PATTERN_SEED = 9;
constexpr int IMPERFECT_TEXTURE_SEED = 2;
constexpr int VERTICAL_PATTERN_SEED = 7;
constexpr int GRID_PATTERN_SEED = 8;
constexpr int DARK_BACKGROUND_SEED = 1;
constexpr int DARK_BACKGROUND_MEASURE_SEED = 300;
constexpr int BLACK_BACKGROUND_MEASURE_SEED = 200;
constexpr int LOW_CONTRAST_SEED = 400;
constexpr int VERTICAL_PATTERN_MEASURE_SEED = 500;
constexpr int GRID_PATTERN_MEASURE_SEED = 600;
constexpr int IMPERFECT_PATTERN_MEASURE_SEED = 700;
constexpr int OUT_OF_MARGIN_PATTERN_MEASURE_SEED = 800;
constexpr int OUT_OF_MARGIN_TEXTURE_MEASURE_SEED = 801;
constexpr int IMPERFECT_PATTERN_PERIOD = 30;
constexpr int OUT_OF_MARGIN_PATTERN_PERIOD = 75;
constexpr int NOISE_SCALES_MAXIMUM = 128;
constexpr int SEED_MULTIPLIER = 10;
constexpr int CSV_BUFFER_SIZE = 64;
constexpr unsigned SEED_STRIDE = 10;
constexpr double PERCENTAGES[] = {0.3, 0.5, 0.7, 0.75, 0.8, 0.9};
constexpr double ALMOST_BLACK_PERCENTAGES[] = {0.3, 0.5, 0.7, 0.8, 0.9, 0.95};
constexpr double CONTRAST_AMPLITUDES[] = {3000.0, 600.0, 300.0, 150.0, 75.0, 40.0, 20.0};
constexpr int VERTICAL_PERIODS[] = {20, 30, 40, 60, 75, 120};
constexpr int GRID_PERIODS[] = {20, 40, 75};
constexpr double TEXTURE_WEIGHTS[] = {0.05, 0.15, 0.3};
constexpr double OUT_OF_MARGIN_OFFSETS[] = {60.0, 70.0};
constexpr double NCC_THRESHOLD_STRICT = 0.5;
constexpr double DISPERSION_THRESHOLD_STRICT = 0.25;
constexpr double NCC_THRESHOLD = 0.3;
constexpr double DISPERSION_THRESHOLD = 0.5;
constexpr double AUTOSIM_THRESHOLD_LOOSE = 0.8;
constexpr double AUTOSIM_THRESHOLD = 0.6;
constexpr int TILE_ORIGIN_X = 300;
constexpr int TILE_ORIGIN_Y = 300;
constexpr std::array<std::array<double, 2>, DRAW_COUNT> RESIDUALS = {
    std::array<double, 2>{3.37, 12.62}, std::array<double, 2>{-8.71, -4.15}, std::array<double, 2>{14.2, 27.9}};
constexpr std::array<double, 2> OUT_OF_MARGIN_OFFSET_X = {3.37, 0.62};
constexpr std::array<double, 2> TEXTURE_OUT_OF_MARGIN_OFFSET = {3.37, 70.62};

// ---------------------------------------------------------------- génération

cv::Mat decalerFourier(const cv::Mat& image, cv::Point2d f) {
    cv::Mat canal, spectre, decale;
    image.convertTo(canal, CV_64F);
    const int height = image.rows, width = image.cols;
    cv::Mat rampe(height, width, CV_64FC2);
    for (int v = 0; v < height; ++v)
        for (int u = 0; u < width; ++u) {
            const double frequencyU = (u <= width / 2 ? u : u - width) / static_cast<double>(width);
            const double frequencyV = (v <= height / 2 ? v : v - height) / static_cast<double>(height);
            const double phase = 2 * CV_PI * (frequencyU * f.x + frequencyV * f.y);
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
    for (int echelle = 1; echelle <= NOISE_SCALES_MAXIMUM; echelle *= 2) {
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

struct CasEtude {
    std::string famille;
    std::string nom;
    Scene scene;
    unsigned graine;
    std::optional<cv::Point2d> vrai = std::nullopt;
};

struct MesureEntree {
    const cv::Mat& imageA;
    const cv::Mat& imageB;
    const PairContext& context;
    cv::Point2d attendu;
};

cv::Size sceneSize() {
    return {TILE_SIDE + 2 * TILE_ORIGIN_X + SCENE_PADDING, TILE_SIDE + NOMINAL_STEP + 2 * TILE_ORIGIN_Y + SCENE_PADDING};
}

// Paire A, B (16 bits) : A au coin COIN_A, B à COIN_A + (vrai.x, PAS + vrai.y). Bruit capteur indépendant.
std::pair<cv::Mat, cv::Mat> paire(const Scene& s, cv::Point2d vrai, unsigned graine) {
    const cv::Point2d cornerB(TILE_ORIGIN_X + vrai.x, TILE_ORIGIN_Y + NOMINAL_STEP + vrai.y);
    const cv::Point integerCorner(static_cast<int>(std::floor(cornerB.x)), static_cast<int>(std::floor(cornerB.y)));
    const cv::Rect rectA(cv::Point(TILE_ORIGIN_X, TILE_ORIGIN_Y), cv::Size(TILE_SIDE, TILE_SIDE));
    const cv::Rect rectB(integerCorner, cv::Size(TILE_SIDE, TILE_SIDE));
    const cv::Rect zone = cv::Rect(rectB.tl() - cv::Point(FOURIER_MARGIN, FOURIER_MARGIN),
                                   rectB.size() + cv::Size(2 * FOURIER_MARGIN, 2 * FOURIER_MARGIN))
                          & cv::Rect({0, 0}, s.image.size());
    const cv::Point2d fraction = cornerB - cv::Point2d(integerCorner);
    cv::Mat a = s.image(rectA).clone();
    cv::Mat b = decalerFourier(s.image(zone), fraction)(rectB - zone.tl()).clone();

    cv::RNG rng(graine);
    for (cv::Mat* m : {&a, &b}) {
        cv::Mat n(m->size(), CV_64F);
        rng.fill(n, cv::RNG::NORMAL, 0, SENSOR_NOISE);
        *m += n;
    }
    cv::Mat imageA16, imageB16;
    a.convertTo(imageA16, CV_16U);
    b.convertTo(imageB16, CV_16U);
    // Noir exact hors de l'œuvre (après le bruit : un scan met ces pixels à 0), au pixel près pour B
    if (!s.exterieur.empty()) {
        imageA16.setTo(0, s.exterieur(rectA));
        const cv::Rect roundedRect(cv::Point(static_cast<int>(std::lround(cornerB.x)),
                                              static_cast<int>(std::lround(cornerB.y))),
                                    cv::Size(TILE_SIDE, TILE_SIDE));
        imageB16.setTo(0, s.exterieur(roundedRect));
    }
    return {imageA16, imageB16};
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
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

void assessPipeline(Resultat& result, const cv::Mat& imageA, const cv::Mat& imageB, const PairContext& context) {
    CorrelationPhase defaut;
    Matches m;
    result.accepte = defaut.match(imageA, imageB, context, m);
    result.raison = defaut.getFailureReason();
    if (result.accepte) {
        const auto confidence = StepsNs::PhaseCorrelationStepClass::checkConfidence(
            m, imageA, StepsNs::ConfidenceContext{Direction::Bottom, OVERLAP, {}, {}});
        result.pipeline = confidence.reason.empty();
        if (!confidence.reason.empty()) result.raison = confidence.reason;
    }
}

void measureUnfiltered(Resultat& result, const MesureEntree& input) {
    PhaseCorrelationNs::Parameters sans;
    sans.minimumPeakRatio = 0;
    EstimationRecouvrement premiere(sans);
    const auto estimate = premiere.estimate(input.imageA, input.imageB, input.context);
    if (!estimate) return;
    result.rapport = estimate->peakRatio;
    PairContext refinedContext = input.context;
    refinedContext.estimatedTranslation = estimate->translationBInA;
    CorrelationPhase seconde(sans);
    Matches m2;
    if (seconde.match(input.imageA, input.imageB, refinedContext, m2)) {
        result.mesure = true;
        result.t = {m2.translationX, m2.translationY};
    } else {
        result.mesure = true;   // deuxième passe en échec : on garde la position entière de la première
        result.t = estimate->translationBInA;
    }
    result.erreur = std::hypot(result.t.x - input.attendu.x, result.t.y - input.attendu.y);
    result.nbPoints = static_cast<int>(m2.pointsInA.size());
    std::vector<double> ncc, disp;
    for (size_t i = 0; i < m2.pointsInA.size(); ++i) {
        ncc.push_back(m2.confidenceScores[i]);
        const cv::Point2d delta = cv::Point2d(m2.pointsInA[i] - m2.pointsInB[i]) - result.t;
        disp.push_back(std::hypot(delta.x, delta.y));
    }
    result.ncc = mediane(ncc);
    result.dispersion = mediane(disp);
}

Resultat mesurer1(const CasEtude& studyCase, cv::Point2d truth, unsigned seed) {
    const auto [IMAGE_A, IMAGE_B] = paire(studyCase.scene, truth, seed);
    const PairContext context{Direction::Bottom, OVERLAP};
    const cv::Point2d expected(truth.x, NOMINAL_STEP + truth.y);
    Resultat result{studyCase.famille, studyCase.nom, truth};
    result.autoSim = StepsNs::PhaseCorrelationStepClass::selfSimilarity(IMAGE_A, Direction::Bottom, OVERLAP, 0);
    assessPipeline(result, IMAGE_A, IMAGE_B, context);
    measureUnfiltered(result, {IMAGE_A, IMAGE_B, context, expected});
    return result;
}

// Résidus (dans la marge du rail, ±50 px) des tirages 0, 1, 2 : l'alias d'un motif tombe à des endroits différents
// NB_TIRAGES mesures du même cas. `vrai` fixé (motif hors marge) ou, s'il est vide, pris dans RESIDUS.
void mesurer(std::vector<Resultat>& res, const CasEtude& studyCase) {
    for (int drawIndex = 0; drawIndex < DRAW_COUNT; ++drawIndex) {
        const auto& residual = RESIDUALS[static_cast<std::size_t>(drawIndex)];
        const cv::Point2d truth = studyCase.vrai ? *studyCase.vrai : cv::Point2d(residual[0], residual[1]);
        res.push_back(mesurer1(studyCase, truth, studyCase.graine * SEED_STRIDE + static_cast<unsigned>(drawIndex)));
    }
}

// ---------------------------------------------------------------- cas

cv::Mat niveaux(const cv::Mat& textureImage, double low, double high) {
    return textureImage * (high - low) + low;
}

void ajouterReferences(std::vector<Resultat>& results) {
    for (unsigned seed : {1u, 2u, 3u}) {
        Scene scene{niveaux(texture(sceneSize(), seed), IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
        mesurer(results, {"reference", "texture graine " + std::to_string(seed), scene, REFERENCE_SEED_BASE + seed});
    }
}

void ajouterFondsNoirs(std::vector<Resultat>& results) {
    for (double portion : PERCENTAGES) {
        Scene scene{niveaux(texture(sceneSize(), DARK_BACKGROUND_SEED), IMAGE_MINIMUM, IMAGE_MAXIMUM),
                    cv::Mat::zeros(sceneSize(), CV_8U)};
        scene.exterieur.colRange(0, TILE_ORIGIN_X + static_cast<int>(portion * TILE_SIDE)).setTo(MASK_VALUE);
        mesurer(results, {"fond_noir", "noir sur " + std::to_string(static_cast<int>(portion * PERCENT)) + " %",
                          scene, BLACK_BACKGROUND_MEASURE_SEED});
    }
}

void ajouterFondsPresqueNoirs(std::vector<Resultat>& results) {
    for (double portion : ALMOST_BLACK_PERCENTAGES) {
        cv::Mat image = niveaux(texture(sceneSize(), DARK_BACKGROUND_SEED), IMAGE_MINIMUM, IMAGE_MAXIMUM);
        image.colRange(0, TILE_ORIGIN_X + static_cast<int>(portion * TILE_SIDE)).setTo(DARK_BACKGROUND_VALUE);
        Scene scene{image, {}};
        mesurer(results, {"fond_presque_noir",
                          "uni + bruit sur " + std::to_string(static_cast<int>(portion * PERCENT)) + " %",
                          scene, DARK_BACKGROUND_MEASURE_SEED});
    }
}

void ajouterTexturesPeuContrastees(std::vector<Resultat>& results) {
    for (double amplitude : CONTRAST_AMPLITUDES) {
        Scene scene{niveaux(texture(sceneSize(), DARK_BACKGROUND_SEED), DARK_BACKGROUND_VALUE,
                            DARK_BACKGROUND_VALUE + amplitude), {}};
        char caseDescription[CSV_BUFFER_SIZE];
        std::snprintf(caseDescription, sizeof caseDescription, "contraste/bruit = %.2f", amplitude / SENSOR_NOISE);
        mesurer(results, {"sombre_peu_contraste", caseDescription, scene, LOW_CONTRAST_SEED});
    }
}

void ajouterMotifsVerticaux(std::vector<Resultat>& results) {
    for (int period : VERTICAL_PERIODS) {
        Scene scene{niveaux(motif(sceneSize(), 0, period, VERTICAL_PATTERN_SEED), IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
        mesurer(results, {"motif_vertical", "periode " + std::to_string(period) + " px", scene,
                          VERTICAL_PATTERN_MEASURE_SEED});
    }
}

void ajouterMotifsEnGrille(std::vector<Resultat>& results) {
    for (int period : GRID_PERIODS) {
        Scene scene{niveaux(motif(sceneSize(), period, period, GRID_PATTERN_SEED), IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
        mesurer(results, {"motif_grille", "grille " + std::to_string(period) + " px", scene,
                          GRID_PATTERN_MEASURE_SEED});
    }
}

void ajouterMotifsImparfaits(std::vector<Resultat>& results) {
    for (double textureWeight : TEXTURE_WEIGHTS) {
        const cv::Mat patternImage = motif(sceneSize(), IMPERFECT_PATTERN_PERIOD, IMPERFECT_PATTERN_PERIOD,
                                           IMPERFECT_PATTERN_SEED);
        const cv::Mat textureImage = texture(sceneSize(), IMPERFECT_TEXTURE_SEED);
        Scene scene{niveaux(patternImage * (1 - textureWeight) + textureImage * textureWeight,
                            IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
        char caseDescription[CSV_BUFFER_SIZE];
        std::snprintf(caseDescription, sizeof caseDescription, "grille 30 px + texture %.0f %%", textureWeight * PERCENT);
        mesurer(results, {"motif_imparfait", caseDescription, scene, IMPERFECT_PATTERN_MEASURE_SEED});
    }
}

void ajouterCasHorsMarge(std::vector<Resultat>& results) {
    for (double offset : OUT_OF_MARGIN_OFFSETS) {
        Scene scene{niveaux(motif(sceneSize(), 0, OUT_OF_MARGIN_PATTERN_PERIOD, VERTICAL_PATTERN_SEED),
                            IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
        char caseDescription[CSV_BUFFER_SIZE];
        std::snprintf(caseDescription, sizeof caseDescription, "periode 75, residu %.0f px (hors marge 50)", offset);
        const cv::Point2d truth(OUT_OF_MARGIN_OFFSET_X[0], offset + OUT_OF_MARGIN_OFFSET_X[1]);
        mesurer(results, {"motif_hors_marge", caseDescription, scene, OUT_OF_MARGIN_PATTERN_MEASURE_SEED, truth});
    }
    Scene scene{niveaux(texture(sceneSize(), DARK_BACKGROUND_SEED), IMAGE_MINIMUM, IMAGE_MAXIMUM), {}};
    const cv::Point2d truth(TEXTURE_OUT_OF_MARGIN_OFFSET[0], TEXTURE_OUT_OF_MARGIN_OFFSET[1]);
    mesurer(results, {"motif_hors_marge", "texture, residu 70 px (hors marge 50)", scene,
                      OUT_OF_MARGIN_TEXTURE_MEASURE_SEED, truth});
}

std::vector<Resultat> etude() {
    std::vector<Resultat> results;
    ajouterReferences(results);
    ajouterFondsNoirs(results);
    ajouterFondsPresqueNoirs(results);
    ajouterTexturesPeuContrastees(results);
    ajouterMotifsVerticaux(results);
    ajouterMotifsEnGrille(results);
    ajouterMotifsImparfaits(results);
    ajouterCasHorsMarge(results);
    return results;
}

bool juste(const Resultat& r) { return r.mesure && r.erreur <= MAXIMUM_ERROR; }

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
std::vector<Regle> reglesRapport() {
    return {
    {"PIPELINE (PhaseCorrelationStep, defaut)", [](const Resultat& r) { return r.pipeline; }},
    {"actuelle (rapport >= 1.5)", [](const Resultat& r) { return r.accepte; }},
    {"rapport >= 2", [](const Resultat& r) { return r.accepte && r.rapport >= 2; }},
    {"rapport >= 3", [](const Resultat& r) { return r.accepte && r.rapport >= 3; }},
    };
}

std::vector<Regle> reglesConfiance() {
    return {
    {"actuelle + ncc >= 0.5", [](const Resultat& r) {
         return r.accepte && r.nbPoints > 0 && r.ncc >= NCC_THRESHOLD_STRICT;
     }},
    {"actuelle + dispersion <= 0.25", [](const Resultat& r) {
         return r.accepte && r.nbPoints > 0 && r.dispersion <= DISPERSION_THRESHOLD_STRICT;
     }},
    {"actuelle + ncc >= 0.5 + disp <= 0.25", [](const Resultat& r) {
         return r.accepte && pointsCoherents(r, NCC_THRESHOLD_STRICT, DISPERSION_THRESHOLD_STRICT);
     }},
    {"rapport >= 2 + ncc >= 0.5 + disp <= 0.25", [](const Resultat& r) {
         return r.accepte && r.rapport >= 2 && pointsCoherents(r, NCC_THRESHOLD_STRICT, DISPERSION_THRESHOLD_STRICT);
     }},
    {"actuelle + ncc >= 0.3 + disp <= 0.5", [](const Resultat& r) {
         return r.accepte && pointsCoherents(r, NCC_THRESHOLD, DISPERSION_THRESHOLD);
     }},
    {"actuelle + autosim < 0.8", [](const Resultat& r) { return r.accepte && !(r.autoSim >= AUTOSIM_THRESHOLD_LOOSE); }},
    {"actuelle + autosim < 0.6", [](const Resultat& r) { return r.accepte && !(r.autoSim >= AUTOSIM_THRESHOLD); }},
    {"actuelle + ncc>=0.3 + disp<=0.5 + autosim<0.8", [](const Resultat& r) {
         return r.accepte && pointsCoherents(r, NCC_THRESHOLD, DISPERSION_THRESHOLD)
                && !(r.autoSim >= AUTOSIM_THRESHOLD_LOOSE);
     }},
    {"actuelle + ncc>=0.3 + disp<=0.5 + autosim<0.6", [](const Resultat& r) {
         return r.accepte && pointsCoherents(r, NCC_THRESHOLD, DISPERSION_THRESHOLD)
                && !(r.autoSim >= AUTOSIM_THRESHOLD);
     }},
    };
}

std::vector<Regle> regles() {
    std::vector<Regle> rules = reglesRapport();
    const auto confidenceRules = reglesConfiance();
    rules.insert(rules.end(), confidenceRules.begin(), confidenceRules.end());
    return rules;
}

} // namespace EtudeConfianceNs

void afficherMesures(const std::vector<EtudeConfianceNs::Resultat>& results) {
    std::printf("%-22s %-40s %8s %7s %8s %6s %6s %7s  %s\n", "famille", "cas", "erreur", "rapport", "autosim",
                "points", "ncc", "disp", "decision actuelle (seuil 1.5)");
    for (const auto& result : results)
        std::printf("%-22s %-40s %8.3f %7.2f %8.2f %6d %6.3f %7.3f  %s\n", result.famille.c_str(),
                    result.cas.c_str(), result.erreur, result.rapport, result.autoSim, result.nbPoints, result.ncc,
                    result.dispersion, EtudeConfianceNs::verdict(result).c_str());
}

void afficherComparaison(const std::vector<EtudeConfianceNs::Resultat>& results) {
    std::printf("\nComparaison des regles (%zu mesures, juste = erreur <= %.1f px)\n",
                results.size(), EtudeConfianceNs::MAXIMUM_ERROR);
    std::printf("%-42s %7s %13s %15s %14s\n", "regle", "fiable", "FAUX ACCEPTE", "rejet justifie", "rejet inutile");
    for (const auto& rule : EtudeConfianceNs::regles()) {
        int totals[4] = {};
        for (const auto& result : results) {
            const bool accepted = rule.accepte(result);
            const bool correct = EtudeConfianceNs::juste(result);
            ++totals[accepted ? (correct ? 0 : 1) : (correct ? 3 : 2)];
        }
        std::printf("%-42s %7d %13d %15d %14d\n", rule.nom.c_str(), totals[0], totals[1], totals[2], totals[3]);
    }
}

bool ecrireCsv(const std::string& path, const std::vector<EtudeConfianceNs::Resultat>& results) {
    std::ofstream csv(path);
    if (!csv) return false;
    csv << "famille,cas,vrai_dx,vrai_dy,accepte,mesure_dx,mesure_dy,erreur,rapport,nb_points,ncc_mediane,"
           "dispersion_mediane,autosimilarite,pipeline,verdict,raison\n";
    for (const auto& result : results) {
        std::string reason = result.raison;
        std::replace(reason.begin(), reason.end(), '"', '\'');
        csv << result.famille << ",\"" << result.cas << "\"," << result.vrai.x << ','
            << EtudeConfianceNs::NOMINAL_STEP + result.vrai.y << ',' << result.accepte << ','
            << result.t.x << ',' << result.t.y << ',' << result.erreur << ',' << result.rapport << ','
            << result.nbPoints << ',' << result.ncc << ',' << result.dispersion << ',' << result.autoSim << ','
            << result.pipeline << ',' << EtudeConfianceNs::verdict(result) << ",\"" << reason << "\"\n";
    }
    return static_cast<bool>(csv);
}

// NOLINTNEXTLINE(bugprone-exception-escape) : programme d'étude, une erreur peut l'arrêter.
int main(int argc, char** argv) {
    auto results = EtudeConfianceNs::etude();
    afficherMesures(results);
    afficherComparaison(results);
    if (argc > 1 && !ecrireCsv(argv[1], results)) {
        std::fprintf(stderr, "Impossible d'ecrire le fichier CSV: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
