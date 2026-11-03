// tests/phase_correlation/test_phase_correlation.cpp
// Vérification de PhaseCorrelationMatcherClass (voir docs/algorithms/stitching/phase_correlation/PhaseCorrelation.md §7).
//
// Tests à vérité exacte (GoogleTest, ctest -L unit) : deux tuiles découpées dans une texture générée à des
// positions non entières connues, dans les 4 directions, puis les deux passes (recouvrement réel différent du
// nominal, première passe faite par le pipeline : OverlapEstimatorClass) et les cas limites.

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "core/data/algorithm_types.h"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using DataNs::Direction;
using DataNs::Matches;
using DataNs::PairContext;
using OverlapEstimationNs::OverlapEstimatorClass;
using PhaseCorrelationNs::PhaseCorrelationMatcherClass;

constexpr double TOLERANCE_TRANSLATION = 0.05;  // px : écart maximal à la vérité pour la translation globale
constexpr double TOLERANCE_POINTS      = 0.5;   // px : écart maximal de pointsA − pointsB à la vérité
constexpr int    COTE_TUILE            = 1000;  // px : tuiles carrées découpées dans la source
constexpr double RECOUVREMENT          = 0.2;
constexpr int    PERIODE_MOTIF         = 75;    // px : motif répété, période > marge du rail (50 px ici)
constexpr int    MARGE_FOURIER         = 200;   // px : bord ajouté autour de B pour le décalage par FFT
constexpr double RECOUVREMENT_HORS_MARGE = 0.27; // recouvrement réel d'une paire annoncée à 20 % : 70 px hors marge
constexpr double MARGE_ELARGIE         = 100;   // px : marge du rail qui couvre ces 70 px
constexpr double RECOUVREMENTS_REELS[] = {0.23, 0.17};  // annoncés à 20 % : 30 px de part et d'autre, dans la marge
constexpr double ERREUR_PREMIERE_PASSE = 1.0;   // px : écart maximal de la première passe (au pixel) à la vérité
const cv::Point2d ERREUR_PIPELINE(2, -2);       // px : translation du pipeline un peu fausse, dans la marge (4 px)
const cv::Point2d ERREUR_HORS_MARGE(0, 10);     // px : translation du pipeline trop fausse pour la deuxième passe
const cv::Point2d SANS_RECOUVREMENT(0, 2000);   // px : B placée hors de A

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool condition, const std::string& message) {
    EXPECT_TRUE(condition) << message;
}

std::string format(const char* modele, double a, double b = 0, double c = 0, double d = 0) {
    char tampon[256];
    std::snprintf(tampon, sizeof tampon, modele, a, b, c, d);
    return tampon;
}

// Image décalée d'une fraction de pixel : resultat(y, x) = image(y + f.y, x + f.x), même type.
// Décalage exact par la FFT (théorème du décalage). Une interpolation bicubique (cv::warpAffine) ne convient
// pas : elle déforme la phase et biaise la vérité de 0,1 à 0,25 px, plus que la précision à vérifier.
cv::Mat decalerFourier(const cv::Mat& image, cv::Point2d f) {
    std::vector<cv::Mat> canaux;
    cv::split(image, canaux);
    const int h = image.rows, w = image.cols;
    cv::Mat rampe(h, w, CV_64FC2);
    for (int v = 0; v < h; ++v)
        for (int u = 0; u < w; ++u) {
            const double fu = (u <= w / 2 ? u : u - w) / static_cast<double>(w);
            const double fv = (v <= h / 2 ? v : v - h) / static_cast<double>(h);
            const double phase = 2 * CV_PI * (fu * f.x + fv * f.y);
            rampe.at<cv::Vec2d>(v, u) = {std::cos(phase), std::sin(phase)};
        }
    for (auto& canal : canaux) {
        cv::Mat spectre, decale;
        canal.convertTo(canal, CV_64F);
        cv::dft(canal, spectre, cv::DFT_COMPLEX_OUTPUT);
        cv::mulSpectrums(spectre, rampe, spectre, 0);
        cv::idft(spectre, decale, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
        decale.convertTo(canal, image.depth());   // arrondi et écrêtage dans le type d'origine
    }
    cv::Mat resultat;
    cv::merge(canaux, resultat);
    return resultat;
}

// Deux tuiles de COTE_TUILE px découpées dans la source : A au coin entier `coinA`, B au coin
// `coinA + vrai` (vrai non entier). Seule B est décalée : A est une découpe exacte de la source.
std::pair<cv::Mat, cv::Mat> decouperPaire(const cv::Mat& source, cv::Point coinA, cv::Point2d vrai) {
    const cv::Point entier(static_cast<int>(std::floor(vrai.x)), static_cast<int>(std::floor(vrai.y)));
    const cv::Rect rectA(coinA, cv::Size(COTE_TUILE, COTE_TUILE)), rectB = rectA + entier;
    // Le décalage se fait sur une zone un peu plus grande que B : les bords de la FFT (périodique) restent loin
    const cv::Rect zone = cv::Rect(rectB.tl() - cv::Point(MARGE_FOURIER, MARGE_FOURIER),
                                   rectB.size() + cv::Size(2 * MARGE_FOURIER, 2 * MARGE_FOURIER))
                          & cv::Rect(cv::Point(), source.size());
    const cv::Mat decale = decalerFourier(source(zone), vrai - cv::Point2d(entier));
    return {source(rectA).clone(), decale(rectB - zone.tl()).clone()};
}

// Fausse peinture 16 bits : bruit à plusieurs échelles (du grain aux grandes taches)
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

// Motif de PERIODE_MOTIF px répété sur toute la taille
cv::Mat motifPeriodique(cv::Size taille) {
    const cv::Mat motif = textureGeneree(cv::Size(PERIODE_MOTIF, PERIODE_MOTIF), 7);
    cv::Mat resultat;
    cv::repeat(motif, taille.height / PERIODE_MOTIF + 1, taille.width / PERIODE_MOTIF + 1, resultat);
    return resultat(cv::Rect(cv::Point(), taille)).clone();
}

double mediane(std::vector<double> v) {
    if (v.empty()) return NAN;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

// Écarts |pointsA − pointsB − attendu| de chaque paire de points
std::vector<double> ecartsPoints(const Matches& m, cv::Point2d attendu) {
    std::vector<double> ecarts;
    for (size_t i = 0; i < m.pointsInA.size(); ++i) {
        const cv::Point2d d = cv::Point2d(m.pointsInA[i] - m.pointsInB[i]) - attendu;
        ecarts.push_back(std::hypot(d.x, d.y));
    }
    return ecarts;
}

// Une paire découpée dans la source : B est à `vrai` (coin de B dans le repère de A) par rapport à A
void testerPaire(const std::string& nom, const cv::Mat& source, cv::Point coinA, cv::Point2d vrai, Direction dir) {
    std::cout << nom << "  (vérité x = " << format("%.4f, y = %.4f)", vrai.x, vrai.y) << '\n';
    const auto [A, B] = decouperPaire(source, coinA, vrai);

    PhaseCorrelationMatcherClass cp;
    Matches m;
    const auto debut = std::chrono::steady_clock::now();
    const bool ok = cp.match(A, B, {dir, static_cast<float>(RECOUVREMENT)}, m);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - debut).count();

    verifier(ok, format("match renvoie true (%.0f ms)", ms));
    if (!ok) return;
    const double ex = m.translationX - vrai.x, ey = m.translationY - vrai.y;
    verifier(std::abs(ex) <= TOLERANCE_TRANSLATION && std::abs(ey) <= TOLERANCE_TRANSLATION,
             format("translation dx = %.4f, dy = %.4f (écart %+.4f, %+.4f px)", m.translationX, m.translationY, ex, ey));

    const bool taillesCoherentes = m.pointsInA.size() == m.pointsInB.size() && m.pointsInA.size() == m.confidenceScores.size();
    verifier(taillesCoherentes && !m.pointsInA.empty(), format("%.0f points", static_cast<double>(m.pointsInA.size())));
    if (!taillesCoherentes || m.pointsInA.empty()) return;

    const auto ecarts = ecartsPoints(m, vrai);
    const double pire = *std::max_element(ecarts.begin(), ecarts.end());
    verifier(pire <= TOLERANCE_POINTS,
             format("pointsA - pointsB ~ vérité : écart médian %.3f px, max %.3f px", mediane(ecarts), pire));

    const cv::Rect tuile(0, 0, COTE_TUILE, COTE_TUILE);
    bool dansLesTuiles = true, confiancesValides = true;
    for (size_t i = 0; i < m.pointsInA.size(); ++i) {
        dansLesTuiles &= tuile.contains(cv::Point(m.pointsInA[i])) && tuile.contains(cv::Point(m.pointsInB[i]));
        confiancesValides &= m.confidenceScores[i] >= 0.f && m.confidenceScores[i] <= 1.f;
    }
    verifier(dansLesTuiles, "points dans les tuiles");
    verifier(confiancesValides && std::is_sorted(m.confidenceScores.rbegin(), m.confidenceScores.rend()),
             "scores dans [0, 1], triés par ordre décroissant");
}

// Les 4 directions sur une même source, avec des résidus non entiers différents autour du rail
void testerDirections(const std::string& nomSource, const cv::Mat& source) {
    const double pas = COTE_TUILE * (1 - RECOUVREMENT);
    const cv::Point2d centre(source.cols / 2.0 - COTE_TUILE / 2.0, source.rows / 2.0 - COTE_TUILE / 2.0);
    std::cout << "\n=== Source : " << nomSource << " (" << source.cols << " x " << source.rows << ") ===\n";
    if (source.cols < COTE_TUILE + pas + 2 * 60 + MARGE_FOURIER || source.rows < COTE_TUILE + pas + 2 * 60 + MARGE_FOURIER) {
        verifier(false, "source trop petite pour deux tuiles de 1000 px");
        return;
    }
    // Chaque paire est centrée sur la source : A est décalée d'un demi-pas à l'opposé de B
    const std::vector<std::tuple<std::string, cv::Point2d, Direction>> cas = {
        {"Bottom", {7.373, pas + 12.8167},    Direction::Bottom},
        {"Right",  {pas - 21.6641, -9.1829},  Direction::Right},
        {"Top",    {-4.5532, -pas + 17.2918}, Direction::Top},
        {"Left",   {-pas - 30.7094, 3.2357},  Direction::Left},
    };
    for (const auto& [nom, vrai, dir] : cas)
        testerPaire(nom, source, cv::Point(centre - vrai / 2), vrai, dir);
}

// Recouvrement réel différent du nominal (20 % annoncés) : la première passe (OverlapEstimatorClass) retrouve
// la position au pixel et le recouvrement réel ; la deuxième mesure la translation sur toute la zone commune
// réelle, que la première passe soit faite par match() ou fournie par le pipeline (estimatedTranslation)
void testerDeuxPasses(const cv::Mat& source) {
    std::cout << "\n=== Première et deuxième passe ===\n";
    const cv::Point coinA(300, 400);
    const PairContext bas{Direction::Bottom, static_cast<float>(RECOUVREMENT)};
    const PairContext haut{Direction::Top, static_cast<float>(RECOUVREMENT)};
    OverlapEstimatorClass estimation;
    PhaseCorrelationMatcherClass cp;

    for (const double reel : RECOUVREMENTS_REELS) {
        const cv::Point2d vrai(-6.4187, COTE_TUILE * (1 - reel) + 0.2913);
        std::cout << format("Recouvrement réel %.0f %% (vérité x = %.4f, y = %.4f)\n", reel * 100, vrai.x, vrai.y);
        const auto [A, B] = decouperPaire(source, coinA, vrai);

        // Première passe : position au pixel près et recouvrement réel
        const auto r = estimation.estimate(A, B, bas);
        verifier(r.has_value(), "première passe : estimer renvoie un résultat " + estimation.getFailureReason());
        if (!r) continue;
        const double recouvrementAttendu = (COTE_TUILE - vrai.y) / COTE_TUILE;
        verifier(std::abs(r->translationBInA.x - vrai.x) <= ERREUR_PREMIERE_PASSE &&
                     std::abs(r->translationBInA.y - vrai.y) <= ERREUR_PREMIERE_PASSE &&
                     r->translationBInA.x == std::round(r->translationBInA.x) && r->translationBInA.y == std::round(r->translationBInA.y),
                 format("première passe : translation entière (%.0f, %.0f)", r->translationBInA.x, r->translationBInA.y));
        verifier(std::abs(r->overlapFraction - recouvrementAttendu) <= ERREUR_PREMIERE_PASSE / COTE_TUILE,
                 format("première passe : recouvrement réel %.2f %% (vérité %.2f %%)", r->overlapFraction * 100.0,
                        recouvrementAttendu * 100));
        const auto rInverse = estimation.estimate(B, A, haut);
        verifier(rInverse && rInverse->translationBInA == -r->translationBInA && rInverse->overlapFraction == r->overlapFraction,
                 "première passe : Top(B, A) = -Bottom(A, B), même recouvrement");

        // Zone commune réelle : toute la hauteur du recouvrement réel, pas les 20 % nominaux
        const auto [za, zb] = OverlapEstimatorClass::computeCommonAreas(A.size(), B.size(), r->translationBInA);
        verifier(za.size() == zb.size() && za.height == COTE_TUILE - static_cast<int>(r->translationBInA.y) &&
                     za.width == COTE_TUILE - static_cast<int>(std::abs(r->translationBInA.x)),
                 format("zone commune : %.0f x %.0f px (bande nominale : %.0f px de haut)", za.width, za.height,
                        COTE_TUILE * RECOUVREMENT));

        // Les deux passes dans match()
        Matches m;
        const bool ok = cp.match(A, B, bas, m);
        verifier(ok && std::abs(m.translationX - vrai.x) <= TOLERANCE_TRANSLATION && std::abs(m.translationY - vrai.y) <= TOLERANCE_TRANSLATION,
                 format("match : dx = %.4f, dy = %.4f (écart %+.4f, %+.4f px)", m.translationX, m.translationY, m.translationX - vrai.x, m.translationY - vrai.y));

        // Première passe faite par le pipeline : même résultat, au bit près
        PairContext pipeline{Direction::Bottom, r->overlapFraction, r->translationBInA};
        Matches mp;
        verifier(cp.match(A, B, pipeline, mp) && mp.translationX == m.translationX && mp.translationY == m.translationY && mp.pointsInA == m.pointsInA &&
                     mp.pointsInB == m.pointsInB,
                 "estimatedTranslation du pipeline : même translation et mêmes points que match() seul");

        // Translation du pipeline un peu fausse : la deuxième passe la rattrape
        pipeline.estimatedTranslation = r->translationBInA + ERREUR_PIPELINE;
        verifier(cp.match(A, B, pipeline, mp) && std::abs(mp.translationX - vrai.x) <= TOLERANCE_TRANSLATION &&
                     std::abs(mp.translationY - vrai.y) <= TOLERANCE_TRANSLATION,
                 format("estimatedTranslation fausse de (%+.0f, %+.0f) px : dx = %.4f, dy = %.4f", ERREUR_PIPELINE.x,
                        ERREUR_PIPELINE.y, mp.translationX, mp.translationY));

        // Trop fausse : false plutôt qu'une translation tronquée au bord de la marge
        pipeline.estimatedTranslation = r->translationBInA + ERREUR_HORS_MARGE;
        const bool okHorsMarge = cp.match(A, B, pipeline, mp);
        verifier(!okHorsMarge && cp.getFailureReason().find("second pass") != std::string::npos,
                 format("estimatedTranslation fausse de 10 px -> false (dx = %.4f, dy = %.4f) : ", mp.translationX, mp.translationY) +
                     cp.getFailureReason());
    }

    // Translation du pipeline qui ne fait pas se superposer les tuiles
    const auto [A, B] = decouperPaire(source, coinA, {0.3, COTE_TUILE * (1 - RECOUVREMENT) + 0.6});
    Matches m;
    const bool okSansRecouvrement = cp.match(A, B, {Direction::Bottom, static_cast<float>(RECOUVREMENT), SANS_RECOUVREMENT}, m);
    verifier(!okSansRecouvrement && cp.getFailureReason().find("actual common area") != std::string::npos,
             "estimatedTranslation sans recouvrement -> false : " + cp.getFailureReason());
}

void testerCasLimites(const cv::Mat& source) {
    std::cout << "\n=== Cas limites ===\n";
    const double pas = COTE_TUILE * (1 - RECOUVREMENT);
    const cv::Point coinA(300, 400);
    const cv::Point2d vrai(7.373, pas + 12.8167);
    const auto [A, B] = decouperPaire(source, coinA, vrai);
    const PairContext bas{Direction::Bottom, static_cast<float>(RECOUVREMENT)};
    PhaseCorrelationMatcherClass cp;
    Matches m, inverse;

    // Échanger A et B en inversant la direction : même calcul, résultat opposé, points échangés
    const bool ok1 = cp.match(A, B, bas, m);
    const bool ok2 = cp.match(B, A, {Direction::Top, static_cast<float>(RECOUVREMENT)}, inverse);
    verifier(ok1 && ok2 && std::abs(m.translationX + inverse.translationX) < 1e-6 && std::abs(m.translationY + inverse.translationY) < 1e-6
                 && m.pointsInA == inverse.pointsInB && m.pointsInB == inverse.pointsInA,
             format("Bottom(A, B) = -Top(B, A) : (%.3f, %.3f) / (%.3f, %.3f)", m.translationX, m.translationY, inverse.translationX, inverse.translationY));

    // Bandes noires reliées au bord : hors de l'œuvre, rien à mesurer
    cv::Mat Anoire = A.clone(), Bnoire = B.clone();
    Anoire.rowRange(COTE_TUILE / 2, COTE_TUILE).setTo(0);
    Bnoire.rowRange(0, COTE_TUILE / 2).setTo(0);
    verifier(!cp.match(Anoire, Bnoire, bas, m), "bandes noires -> false");

    // Bande de B surtout saturée (reflet) : moins de 25 % de pixels exploitables
    cv::Mat Bsaturee = B.clone();
    Bsaturee.rowRange(0, COTE_TUILE / 2).colRange(0, COTE_TUILE * 8 / 10).setTo(cv::Scalar::all(65535));
    verifier(!cp.match(A, Bsaturee, bas, m), "bande de B saturée à 80 % -> false");

    // Tache noire isolée dans la bande de A : vraie donnée, la mesure doit rester juste
    cv::Mat Atache = A.clone();
    cv::circle(Atache, {COTE_TUILE / 2, COTE_TUILE - 100}, 40, cv::Scalar::all(0), cv::FILLED);
    const bool okTache = cp.match(Atache, B, bas, m);
    verifier(okTache && std::abs(m.translationX - vrai.x) < TOLERANCE_TRANSLATION * 2 && std::abs(m.translationY - vrai.y) < TOLERANCE_TRANSLATION * 2,
             format("tache noire isolée dans A : dx = %.3f, dy = %.3f", m.translationX, m.translationY));

    // Entrées impossibles : false, pas d'exception
    const cv::Mat uniforme(COTE_TUILE, COTE_TUILE, A.type(), cv::Scalar::all(20000));
    verifier(!cp.match(uniforme, uniforme, bas, m), "tuiles uniformes -> false");
    verifier(!cp.match(A, B(cv::Rect(0, 0, 900, 900)), bas, m), "tailles différentes -> false");
    verifier(!cp.match(cv::Mat(), B, bas, m), "tuile vide -> false");
    verifier(!cp.match(A, B, {Direction::Bottom, 0.f}, m), "recouvrement 0 -> false");
    verifier(!cp.match(A, B, {Direction::Bottom, 1.5f}, m), "recouvrement 1.5 -> false");
    verifier(cp.getFailureReason().find("nominal overlap") != std::string::npos,
             "getFailureReason : " + cp.getFailureReason());

    // Recouvrement réel de 27 % pour 20 % annoncés : 70 px d'écart, au-delà de la marge du rail (50 px).
    // false, avec un message qui dit d'augmenter la marge ; avec une marge suffisante, la mesure est juste.
    const cv::Point2d vraiHorsMarge(4.2163, COTE_TUILE * (1 - RECOUVREMENT_HORS_MARGE) + 0.3721);
    const auto [Ah, Bh] = decouperPaire(source, coinA, vraiHorsMarge);
    const bool okHorsMarge = cp.match(Ah, Bh, bas, m);
    verifier(!okHorsMarge && cp.getFailureReason().find("Increase the rail margin") != std::string::npos,
             "recouvrement réel 27 % hors marge -> false : " + cp.getFailureReason());
    PhaseCorrelationNs::Parameters margeLarge;
    margeLarge.railMargin = MARGE_ELARGIE;
    PhaseCorrelationMatcherClass cpMargeLarge(margeLarge);
    const bool okMargeLarge = cpMargeLarge.match(Ah, Bh, bas, m);
    verifier(okMargeLarge && cpMargeLarge.getFailureReason().empty() && std::abs(m.translationX - vraiHorsMarge.x) < TOLERANCE_TRANSLATION &&
                 std::abs(m.translationY - vraiHorsMarge.y) < TOLERANCE_TRANSLATION,
             format("même paire, marge = 100 px : dx = %.3f, dy = %.3f (vérité %.3f, %.3f)", m.translationX, m.translationY,
                    vraiHorsMarge.x, vraiHorsMarge.y));

    // nbPoints = 0 : translation seule
    PhaseCorrelationNs::Parameters translationSeule;
    translationSeule.maximumPointCount = 0;
    PhaseCorrelationMatcherClass sansPoints(translationSeule);
    verifier(sansPoints.match(A, B, bas, m) && m.pointsInA.empty() && std::abs(m.translationY - vrai.y) < TOLERANCE_TRANSLATION,
             "nbPoints = 0 -> translation seule, aucun point");

    // Paramètres invalides : erreur de configuration, à la construction
    PhaseCorrelationNs::Parameters fenetreTropPetite;
    fenetreTropPetite.patchSize = 4;
    bool leve = false;
    try { PhaseCorrelationMatcherClass invalide(fenetreTropPetite); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "fenetre = 4 -> std::invalid_argument");
    PhaseCorrelationNs::Parameters fractionHorsDomaine;
    fractionHorsDomaine.minimumValidFraction = 1.5;
    leve = false;
    try { PhaseCorrelationMatcherClass invalide(fractionHorsDomaine); } catch (const std::invalid_argument&) { leve = true; }
    verifier(leve, "fractionValideMin = 1.5 -> std::invalid_argument");

    // Motif périodique : ses copies (tous les 75 px) sont hors de la marge du rail (50 px),
    // seul le pic dans la marge est retenu
    const cv::Mat periodique = motifPeriodique(source.size());
    const cv::Point2d vraiPeriodique(5.0, pas + 20.0);
    const auto [Ap, Bp] = decouperPaire(periodique, coinA, vraiPeriodique);
    const bool okPeriodique = cp.match(Ap, Bp, bas, m);
    verifier(okPeriodique && std::abs(m.translationX - vraiPeriodique.x) < 0.5 && std::abs(m.translationY - vraiPeriodique.y) < 0.5,
             format("motif de période 75 px, vérité (5, 820) : dx = %.2f, dy = %.2f", m.translationX, m.translationY));
}

TEST(PhaseCorrelation, QuatreDirections) { testerDirections("texture générée", textureGeneree({2400, 2400}, 1)); }
TEST(PhaseCorrelation, DeuxPasses) { testerDeuxPasses(textureGeneree({2400, 2400}, 3)); }
TEST(PhaseCorrelation, CasLimites) { testerCasLimites(textureGeneree({2400, 2400}, 2)); }

}
