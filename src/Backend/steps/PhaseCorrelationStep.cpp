#include "steps/PhaseCorrelationStep.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace stitching::steps {

PhaseCorrelationStep::PhaseCorrelationStep(const PhaseCorrelationInput& input) {
    const auto& p = input.parametres;
    StepConfig cfg;
    cfg.name = "phase-correlation";
    cfg.options = {{"float", "overlap", input.overlap},
                   {"int", "niveau", input.niveau},
                   {"float", "lissage", p.lissage},
                   {"float", "seuilRapport", p.seuilRapport},
                   {"float", "lissageLocal", p.lissageLocal},
                   {"float", "ecartMax", p.ecartMax},
                   {"float", "espacementMin", p.espacementMin},
                   {"float", "marge", p.marge},
                   {"float", "fractionValideMin", p.fractionValideMin},
                   {"int", "nbPoints", p.nbPoints},
                   {"int", "fenetre", p.fenetre},
                   {"int", "facteurDft", p.facteurDft},
                   {"float", "nccMin", input.confiance.nccMin},
                   {"float", "dispersionMax", input.confiance.dispersionMax},
                   {"float", "autosimMax", input.confiance.autosimMax}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{"PairMatches", "matches"}};
    config = cfg;
}

cv::Mat PhaseCorrelationStep::lirePixels(const Tuile& tuile, int niveau) {
    cv::Mat image = cv::imread(tuile.getSource().string(), cv::IMREAD_UNCHANGED);   // garde le 16 bits
    // Mêmes réductions successives que PyramidalTiffWriter : le résultat sera celui de readRegion
    for (int n = 0; n < niveau && !image.empty(); ++n)
        cv::resize(image, image, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
    return image;
}

void PhaseCorrelationStep::versPleineResolution(Matches& m, int niveau) {
    if (niveau == 0) return;
    const float s = static_cast<float>(1 << niveau);
    // Le pixel p du niveau n moyenne les pixels [p·s, (p + 1)·s[ : son centre est en (p + 0,5)·s − 0,5
    auto point = [s](cv::Point2f& p) { p = (p + cv::Point2f(0.5f, 0.5f)) * s - cv::Point2f(0.5f, 0.5f); };
    for (auto& p : m.pointsA) point(p);
    for (auto& p : m.pointsB) point(p);
    m.dx *= s;
    m.dy *= s;
}

double PhaseCorrelationStep::autoSimilarite(const cv::Mat& a, Direction dir, float overlap, double marge) {
    // Bande de A du côté de B, en gris float
    const bool bas = dir == Direction::Bottom;
    const int cote = bas ? a.rows : a.cols;
    const int epaisseur = static_cast<int>(std::lround(overlap * cote));
    if (epaisseur <= 0 || epaisseur > cote) return NAN;
    const cv::Rect rect = bas ? cv::Rect(0, a.rows - epaisseur, a.cols, epaisseur)
                              : cv::Rect(a.cols - epaisseur, 0, epaisseur, a.rows);
    cv::Mat gris = a(rect), bande, flou;
    if (gris.channels() > 1) cv::cvtColor(gris, gris, gris.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    gris.convertTo(bande, CV_32F);
    const cv::Mat nonVide = (bande != 0);   // noir exact = hors de l'œuvre
    if (cv::countNonZero(nonVide) < static_cast<int>(bande.total() / 4)) return NAN;

    // Passe-haut : les grandes taches d'une texture normale se ressemblent à quelques px près, pas le grain fin
    cv::GaussianBlur(bande, flou, {0, 0}, 4.0);
    bande -= flou;
    // Écrêtage à ±3 écarts robustes (1,4826 × médiane des |valeurs| non vides) : un bord net (fond / peinture)
    // aurait sinon un poids énorme, et un bord droit se superpose à lui-même quand on le décale le long du bord
    std::vector<float> valeurs;
    for (int y = 0; y < bande.rows; ++y)
        for (int x = 0; x < bande.cols; ++x)
            if (nonVide.at<uchar>(y, x)) valeurs.push_back(std::abs(bande.at<float>(y, x)));
    std::nth_element(valeurs.begin(), valeurs.begin() + valeurs.size() / 2, valeurs.end());
    const float plafond = 3 * 1.4826f * valeurs[valeurs.size() / 2];
    if (plafond > 0) cv::min(cv::max(bande, -plafond), plafond, bande);

    // Décalages cherchés : la marge du rail (même règle par défaut que Preparation::margeRail), hors ±10 px
    constexpr int VOISINAGE = 10;   // px : comme le voisinage exclu autour du pic (Algorithme::RAYON_PIC)
    const double m = marge > 0 ? marge : std::max(0.05 * cote, 48.0);
    const int my = std::min(static_cast<int>(m), (bande.rows - 16) / 2);
    const int mx = std::min(static_cast<int>(m), (bande.cols - 16) / 2);
    if (my <= VOISINAGE || mx <= VOISINAGE) return NAN;   // bande trop étroite pour chercher une répétition
    const cv::Mat centre = bande(cv::Rect(mx, my, bande.cols - 2 * mx, bande.rows - 2 * my));
    cv::Mat carte;   // (2·my + 1) × (2·mx + 1), décalage nul au centre
    cv::matchTemplate(bande, centre, carte, cv::TM_CCOEFF_NORMED);
    carte(cv::Rect(mx - VOISINAGE, my - VOISINAGE, 2 * VOISINAGE + 1, 2 * VOISINAGE + 1)).setTo(-1);
    double max;
    cv::minMaxLoc(carte, nullptr, &max);
    return max;
}

std::string PhaseCorrelationStep::controlerConfiance(const Matches& m, const cv::Mat& a, Direction dir, float overlap,
                                                     const matching::CorrelationPhase::Parametres& p,
                                                     const ControleConfiance& c, bool& pointsVerifies) {
    auto nombre = [](double v) {
        char t[32];
        std::snprintf(t, sizeof t, "%.2f", v);
        return std::string(t);
    };
    auto mediane = [](std::vector<double> v) {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };

    // 1. Points locaux (fond sombre, bruit) : le pic peut être franc mais décalé, les patchs le révèlent.
    pointsVerifies = !m.pointsA.empty();
    const bool controlePoints = c.nccMin > 0 || c.dispersionMax > 0;
    if (!pointsVerifies && controlePoints && p.nbPoints > 0) {
        // Pas de point : soit la zone commune est trop petite pour un patch (rien à vérifier, la paire est gardée),
        // soit elle est assez grande mais aucun point n'a été jugé fiable (bruit, zone sans texture) : rejet.
        // Taille minimale : celle qu'exige Preparation::recouvrement pour chercher des points.
        const int margePatch = p.fenetre / 2 + static_cast<int>(std::ceil(p.ecartMax)) + 1;
        const int minimum = p.fenetre + 2 * margePatch;
        const int largeur = a.cols - static_cast<int>(std::lround(std::abs(m.dx)));
        const int hauteur = a.rows - static_cast<int>(std::lround(std::abs(m.dy)));
        if (largeur >= minimum && hauteur >= minimum)
            return "confiance : aucun point local fiable alors que la zone commune (" + std::to_string(largeur) + " x " +
                   std::to_string(hauteur) + " px) permet d'en placer : bruit ou zone sans texture, translation non vérifiable";
    }
    if (pointsVerifies && controlePoints) {
        std::vector<double> ncc(m.scores.begin(), m.scores.end()), ecarts;
        for (size_t i = 0; i < m.pointsA.size(); ++i) {
            const cv::Point2f d = m.pointsA[i] - m.pointsB[i] - cv::Point2f(m.dx, m.dy);
            ecarts.push_back(std::hypot(d.x, d.y));
        }
        if (c.nccMin > 0 && mediane(ncc) < c.nccMin)
            return "confiance : patchs locaux peu ressemblants (NCC médiane " + nombre(mediane(ncc)) + " < nccMin " +
                   nombre(c.nccMin) + ") : fond sombre ou bruit, la translation peut être décalée de plusieurs px";
        if (c.dispersionMax > 0 && mediane(ecarts) > c.dispersionMax)
            return "confiance : points locaux en désaccord avec la translation (écart médian " + nombre(mediane(ecarts)) +
                   " px > dispersionMax " + nombre(c.dispersionMax) + " px) : fond sombre ou bruit dans une partie de la bande";
    }

    // 2. Motif répété de période inférieure à la marge : plusieurs positions de B sont possibles
    if (c.autosimMax < 1) {
        const double autosim = autoSimilarite(a, dir, overlap, p.marge);
        if (autosim >= c.autosimMax)
            return "confiance : motif répété dans la bande de recouvrement (auto-similarité " + nombre(autosim) +
                   " >= autosimMax " + nombre(c.autosimMax) + ") : mesure ambiguë, elle peut être fausse d'une période";
    }
    return {};
}

bool PhaseCorrelationStep::execute() {
    output = {};
    outputData = Json::object();

    // Relu depuis config : un JSON chargé par Pipeline::configure() remplace les valeurs du constructeur.
    // Option absente (JSON écrit avant son ajout) : valeur par défaut.
    PhaseCorrelationInput input;
    auto lire = [this](const char* cle, auto& valeur) {
        try {
            valeur = option(cle).get<std::decay_t<decltype(valeur)>>();
        } catch (const std::out_of_range&) {}
    };
    try {
        auto& p = input.parametres;
        lire("overlap", input.overlap);
        lire("niveau", input.niveau);
        lire("lissage", p.lissage);
        lire("seuilRapport", p.seuilRapport);
        lire("lissageLocal", p.lissageLocal);
        lire("ecartMax", p.ecartMax);
        lire("espacementMin", p.espacementMin);
        lire("marge", p.marge);
        lire("fractionValideMin", p.fractionValideMin);
        lire("nbPoints", p.nbPoints);
        lire("fenetre", p.fenetre);
        lire("facteurDft", p.facteurDft);
        lire("nccMin", input.confiance.nccMin);
        lire("dispersionMax", input.confiance.dispersionMax);
        lire("autosimMax", input.confiance.autosimMax);
    } catch (const Json::exception& e) {
        outputData["error"] = std::string("invalid option type: ") + e.what();
        return false;
    }

    if (!(input.overlap > 0.f && input.overlap < 1.f)) {
        outputData["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
        return false;
    }
    if (input.niveau < 0 || input.niveau >= NB_NIVEAUX) {
        outputData["error"] = "niveau must be in [0, " + std::to_string(NB_NIVEAUX - 1) + "], got "
                              + std::to_string(input.niveau);
        return false;
    }
    if (!data || !data->has("tiles")) {
        outputData["error"] = "no tiles: run the import step first";
        return false;
    }

    std::optional<matching::CorrelationPhase> matcher;
    try {
        matcher.emplace(input.parametres);
    } catch (const std::invalid_argument& e) {
        outputData["error"] = std::string("invalid parameters: ") + e.what();
        return false;
    }

    // Deux images en RAM au plus
    Json failures = Json::array(), nonVerifiees = Json::array();
    for (const auto& tuile : data->get<Tiles>("tiles")) {
        const std::pair<Tuile*, Direction> voisins[] = {{tuile->getRight(), Direction::Right},
                                                        {tuile->getBottom(), Direction::Bottom}};
        cv::Mat a;   // lue une seule fois pour ses deux voisins
        for (const auto& [voisin, dir] : voisins) {
            if (!voisin) continue;
            if (a.empty()) a = lirePixels(*tuile, input.niveau);
            const cv::Mat b = lirePixels(*voisin, input.niveau);
            if (a.empty() || b.empty()) {
                outputData["error"] = "cannot read " + (a.empty() ? tuile.get() : voisin)->getSource().string();
                return false;
            }

            PairMatches pair{tuile->getId(), voisin->getId(), dir, {}};
            std::string raison;
            bool pointsVerifies = true;
            if (!matcher->match(a, b, PairContext{dir, input.overlap}, pair.matches))
                raison = matcher->raisonEchec();
            else
                raison = controlerConfiance(pair.matches, a, dir, input.overlap, input.parametres, input.confiance,
                                            pointsVerifies);
            if (raison.empty()) {
                if (!pointsVerifies) nonVerifiees.push_back({{"idA", pair.idA}, {"idB", pair.idB}});
                versPleineResolution(pair.matches, input.niveau);
                output.pairs.push_back(std::move(pair));
            } else {
                failures.push_back({{"idA", pair.idA}, {"idB", pair.idB}, {"reason", raison}});
            }
        }
    }

    outputData["niveau"] = input.niveau;
    outputData["pairs"] = output.pairs.size();
    if (!failures.empty()) outputData["failures"] = failures;
    if (!nonVerifiees.empty()) outputData["unverified"] = nonVerifiees;
    if (output.pairs.empty()) {
        outputData["error"] = "no pair could be measured";
        return false;
    }
    data->set("matches", output.pairs);   // pour les steps suivants (RANSAC, placement global)
    return true;
}

} // namespace stitching::steps
