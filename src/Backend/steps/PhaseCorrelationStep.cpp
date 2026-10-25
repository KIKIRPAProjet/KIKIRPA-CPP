#include "steps/PhaseCorrelationStep.h"
#include "core/Config.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <optional>
#include <stdexcept>

namespace stitching::steps {

PhaseCorrelationStep::PhaseCorrelationStep(const PhaseCorrelationInput& input) {
    const auto& p = input.parametres;
    StepConfig cfg;
    cfg.name = "phase-correlation";
    cfg.options = {{"float", "overlap", input.overlap},
                   {"int", "nbPoints", p.nbPoints},
                   {"int", "fenetre", p.fenetre},
                   {"float", "seuilRapport", p.seuilRapport},
                   {"float", "marge", p.marge},
                   {"int", "facteurDft", p.facteurDft}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{"PairMatches", "matches"}};
    config = cfg;
}

bool PhaseCorrelationStep::execute() {
    output = {};
    outputData = Json::object();

    // Relu depuis config : un JSON chargé par Pipeline::configure() remplace les valeurs du constructeur
    PhaseCorrelationInput input;
    input.overlap                 = option("overlap").get<float>();
    input.parametres.nbPoints     = option("nbPoints").get<int>();
    input.parametres.fenetre      = option("fenetre").get<int>();
    input.parametres.seuilRapport = option("seuilRapport").get<double>();
    input.parametres.marge        = option("marge").get<double>();
    input.parametres.facteurDft   = option("facteurDft").get<int>();

    if (!(input.overlap > 0.f && input.overlap < 1.f)) {
        outputData["error"] = "overlap must be in ]0, 1[, got " + std::to_string(input.overlap);
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

    // Pixels pas encore dans Tuile (TIFF pyramidal à venir) : lecture du fichier source, deux images en RAM au plus
    auto lire = [](const Tuile& t) { return cv::imread(t.getSource().string(), cv::IMREAD_UNCHANGED); };

    Json failures = Json::array();
    for (const auto& tuile : data->get<Tiles>("tiles")) {
        const std::pair<Tuile*, Direction> voisins[] = {{tuile->getRight(), Direction::Right},
                                                        {tuile->getBottom(), Direction::Bottom}};
        cv::Mat a;   // lue une seule fois pour ses deux voisins
        for (const auto& [voisin, dir] : voisins) {
            if (!voisin) continue;
            if (a.empty()) a = lire(*tuile);
            const cv::Mat b = lire(*voisin);
            if (a.empty() || b.empty()) {
                outputData["error"] = "cannot read " + (a.empty() ? tuile.get() : voisin)->getSource().string();
                return false;
            }

            PairMatches pair{tuile->getId(), voisin->getId(), dir, {}};
            if (matcher->match(a, b, PairContext{dir, input.overlap}, pair.matches))
                output.pairs.push_back(std::move(pair));
            else
                failures.push_back({{"idA", pair.idA}, {"idB", pair.idB}, {"reason", matcher->raisonEchec()}});
        }
    }

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
