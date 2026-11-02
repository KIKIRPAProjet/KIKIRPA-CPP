#include "steps/PhaseCorrelationStep.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
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
                   {"int", "facteurDft", p.facteurDft}};
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
    Json failures = Json::array();
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
            if (matcher->match(a, b, PairContext{dir, input.overlap}, pair.matches)) {
                versPleineResolution(pair.matches, input.niveau);
                output.pairs.push_back(std::move(pair));
            } else {
                failures.push_back({{"idA", pair.idA}, {"idB", pair.idB}, {"reason", matcher->raisonEchec()}});
            }
        }
    }

    outputData["niveau"] = input.niveau;
    outputData["pairs"] = output.pairs.size();
    if (!failures.empty()) outputData["failures"] = failures;
    if (output.pairs.empty()) {
        outputData["error"] = "no pair could be measured";
        return false;
    }
    data->set("matches", output.pairs);   // pour les steps suivants (RANSAC, placement global)
    return true;
}

} // namespace stitching::steps
