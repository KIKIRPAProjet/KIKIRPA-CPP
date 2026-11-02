// test_pipeline.cpp
// Construction et exécution d'un pipeline à partir du script JSON (core/PipelineBuilder, Step::importConfig).
//
//   test_pipeline
//       Registre des steps, validation du JSON (options, types, enchaînement des entrées), aller-retour
//       exportConfig → buildPipeline, step qui lève une exception, puis un pipeline réel import →
//       phase-correlation sur deux tuiles 16 bits découpées à une position entière connue.
//       Code de retour 0 si tous les tests passent.

#include "core/PipelineBuilder.h"
#include "core/Config.h"
#include "core/Tuile.h"
#include "steps/ImportStep.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace stitching::tests {

constexpr int    LARGEUR_TUILE  = 400;
constexpr int    HAUTEUR_TUILE  = 300;
constexpr int    DECALAGE_B     = 320;    // px : coin de B dans A (recouvrement de 20 %)
constexpr double SIGMA_TEXTURE  = 2.0;    // flou de la texture aléatoire
constexpr double TOLERANCE      = 0.05;   // px

int nbEchecs = 0;

void verifier(bool ok, const std::string& message) {
    std::cout << (ok ? "  OK    " : "  ECHEC ") << message << '\n';
    if (!ok) ++nbEchecs;
}

// Vrai si `f` lève std::invalid_argument dont le message contient `extrait`
template <class F>
bool leveInvalidArgument(F f, const std::string& extrait) {
    try {
        f();
    } catch (const std::invalid_argument& e) {
        const bool trouve = std::string(e.what()).find(extrait) != std::string::npos;
        if (!trouve) std::cout << "        message reçu : " << e.what() << '\n';
        return trouve;
    }
    return false;
}

Json etape(const std::string& nom, Json options = Json::array()) {
    return {{"name", nom}, {"options", std::move(options)}};
}

// std::vector et non Json : avec nlohmann, {x} à un seul élément ne donne pas un tableau
Json script(const std::vector<Json>& etapes) {
    return {{"pipeline", {{"steps", etapes}}}};
}

// Step de test qui lève une exception à l'exécution
class StepQuiLeve : public Step {
public:
    StepQuiLeve() : Step(Json{{"name", "leve"}, {"options", Json::array()}}) {}
    bool execute() override { throw std::runtime_error("boum"); }
};

void testerRegistre() {
    std::cout << "Registre des steps\n";
    const auto noms = registeredSteps();
    verifier(noms.size() == 4, "4 steps enregistrés");
    for (const auto& nom : noms) {
        const auto step = createStep(nom);
        verifier(step && step->getConfig().value("name", "") == nom, "createStep(\"" + nom + "\") a le bon nom");
    }
    verifier(leveInvalidArgument([] { createStep("inconnu"); }, "unknown step 'inconnu'"), "step inconnu refusé");
}

void testerValidation() {
    std::cout << "Validation du script\n";
    verifier(leveInvalidArgument([] { buildPipeline(Json::array()); }, "JSON object"), "script qui n'est pas un objet");
    verifier(leveInvalidArgument([] { buildPipeline({{"pipeline", {{"steps", 3}}}}); }, "pipeline.steps"),
             "steps qui n'est pas un tableau");
    verifier(leveInvalidArgument([] { buildPipeline(script({{{"options", Json::array()}}})); }, "string 'name'"),
             "step sans nom");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("import", {{{"type", "string"}, {"key", "inconnue"}, {"value", "x"}}})})); },
                                 "unknown option 'inconnue'"),
             "option inconnue");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("import", {{{"type", "int"}, {"key", "path"}, {"value", 3}}})})); },
                                 "has type 'string', got 'int'"),
             "type d'option différent du type déclaré");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("import"), etape("phase-correlation", {{{"type", "int"}, {"key", "nbPoints"}, {"value", 2.5}}})})); },
                                 "expects a value of type 'int'"),
             "valeur réelle pour une option entière");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("import", {{{"type", "string"}, {"key", "path"}, {"value", "a"}}, {{"type", "string"}, {"key", "path"}, {"value", "b"}}})})); },
                                 "duplicate option 'path'"),
             "option en double");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("phase-correlation")})); },
                                 "input 'tiles' is not produced"),
             "entrée produite par aucun step précédent");
    verifier(leveInvalidArgument([] { buildPipeline(script({etape("import"), etape("graphcut")})); },
                                 "input 'positions' is not produced"),
             "graphcut sans step qui produit les positions");

    std::cout << "Fusion des options\n";
    const Pipeline p = buildPipeline(script({etape("import", {{{"type", "string"}, {"key", "path"}, {"value", "ailleurs/"}}}),
                                             etape("phase-correlation", {{{"type", "float"}, {"key", "overlap"}, {"value", 0}}})}));
    const Json& importCfg = p.getSteps()[0]->getConfig();
    std::string path, format;
    for (const Json& o : importCfg.at("options")) {
        if (o.at("key") == "path") path = o.at("value").get<std::string>();
        if (o.at("key") == "format") format = o.at("value").get<std::string>();
    }
    verifier(path == "ailleurs/", "option fournie : valeur remplacée");
    verifier(format == steps::ImportInput{}.format, "option absente : valeur par défaut gardée");
    verifier(p.getSteps()[1]->getConfig().at("options").size() == 12, "phase-correlation garde ses 12 options");

    std::cout << "Aller-retour exportConfig -> buildPipeline\n";
    const Pipeline copie = buildPipeline(p.exportConfig());
    verifier(copie.exportConfig() == p.exportConfig(), "même script après relecture");
}

void testerExceptionDansUnStep() {
    std::cout << "Step qui lève une exception\n";
    Pipeline p;
    p.add(std::make_unique<StepQuiLeve>());
    std::string message;
    const bool ok = p.run([&](const std::string& m) { message = m; });
    verifier(!ok, "run() renvoie false");
    verifier(message.find("boum") != std::string::npos, "le message de l'exception est transmis au journal");
}

// Deux tuiles 16 bits img_C00_R00.tif et img_C01_R00.tif découpées dans une texture aléatoire floutée,
// B à DECALAGE_B px à droite de A : vérité exacte
void creerTuiles(const fs::path& dossier) {
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    cv::Mat source(HAUTEUR_TUILE, DECALAGE_B + LARGEUR_TUILE, CV_32F);
    cv::RNG rng(12345);
    rng.fill(source, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(source, source, cv::Size(), SIGMA_TEXTURE);
    cv::normalize(source, source, 2000.0, 60000.0, cv::NORM_MINMAX);
    cv::Mat source16;
    source.convertTo(source16, CV_16U);
    cv::imwrite((dossier / "img_C00_R00.tif").string(), source16(cv::Rect(0, 0, LARGEUR_TUILE, HAUTEUR_TUILE)));
    cv::imwrite((dossier / "img_C01_R00.tif").string(), source16(cv::Rect(DECALAGE_B, 0, LARGEUR_TUILE, HAUTEUR_TUILE)));
}

Json scriptReel(const fs::path& dossier) {
    return script({etape("import", {{{"type", "string"}, {"key", "path"}, {"value", dossier.string()}}}),
                   etape("phase-correlation")});
}

void testerPipelineReel() {
    std::cout << "Pipeline import -> phase-correlation sur deux tuiles 16 bits\n";
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_pipeline";
    creerTuiles(dossier);

    Pipeline p = buildPipeline(scriptReel(dossier));
    std::string journal;
    const bool ok = p.run([&](const std::string& m) { journal += "        " + m + "\n"; });
    verifier(ok, "run() réussit");
    if (!ok) {
        std::cout << journal;
        return;
    }

    const auto& tuiles = p.getData().get<Tiles>("tiles");
    verifier(tuiles.size() == 2, "2 tuiles importées");
    verifier(tuiles.size() == 2 && tuiles[0]->getRight() == tuiles[1].get(), "B est la voisine de droite de A");
    verifier(tuiles.size() == 2 && fs::exists(tuiles[0]->getTiff()), "TIFF pyramidal créé à l'import");

    const auto& paires = p.getData().get<std::vector<PairMatches>>("matches");
    verifier(paires.size() == 1, "1 paire mesurée");
    if (paires.size() != 1) return;
    const Matches& m = paires[0].matches;
    verifier(std::abs(m.dx - DECALAGE_B) < TOLERANCE && std::abs(m.dy) < TOLERANCE,
             "translation (" + std::to_string(m.dx) + ", " + std::to_string(m.dy) + ") = (" +
                 std::to_string(DECALAGE_B) + ", 0)");
    fs::remove_all(dossier);
}

void testerRepriseReelle() {
    std::cout << "Reprise du pipeline réel sur checkpoints\n";
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_pipeline_reprise";
    const fs::path checkpoints = dossier / "checkpoints";
    creerTuiles(dossier);

    Pipeline p = buildPipeline(scriptReel(dossier));
    RunState r;
    r.checkpointDir = checkpoints.string();
    p.setRunState(r);
    std::string journal;
    const auto log = [&](const std::string& m) { journal += m + "\n"; };
    verifier(p.run(log), "run 1 réussi");
    verifier(fs::exists(checkpoints / "00_import" / "tiles.json") && fs::exists(checkpoints / "01_phase-correlation" / "matches.json"),
             "sorties des deux steps écrites");
    const float dx1 = p.getData().get<std::vector<PairMatches>>("matches").at(0).matches.dx;

    journal.clear();
    verifier(p.run(log), "run 2 réussi");
    verifier(journal.find("nothing to do") != std::string::npos, "run 2 : aucun step refait");
    const auto& tuiles = p.getData().get<Tiles>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getRight() == tuiles[1].get() && tuiles[1]->getLeft() == tuiles[0].get(),
             "tuiles et voisins restaurés");
    const auto& paires = p.getData().get<std::vector<PairMatches>>("matches");
    verifier(paires.size() == 1 && paires[0].matches.dx == dx1 && paires[0].dir == Direction::Right,
             "matches restaurés à l'identique");

    // Une image source modifiée périme le checkpoint de l'import, donc de toute la suite
    {
        cv::Mat a = cv::imread((dossier / "img_C00_R00.tif").string(), cv::IMREAD_UNCHANGED);
        a(cv::Rect(0, 0, 10, 10)).setTo(cv::Scalar(1000));
        cv::imwrite((dossier / "img_C00_R00.tif").string(), a);
    }
    journal.clear();
    verifier(p.run(log), "run 3 réussi");
    verifier(journal.find("Step 1/2 OK") != std::string::npos && journal.find("Step 2/2 OK") != std::string::npos,
             "image modifiée : import et appariement refaits");
    fs::remove_all(dossier);
}

} // namespace stitching::tests

int main() {
    using namespace stitching::tests;
    try {
        testerRegistre();
        testerValidation();
        testerExceptionDansUnStep();
        testerPipelineReel();
        testerRepriseReelle();
    } catch (const std::exception& e) {
        std::cout << "  ECHEC exception inattendue : " << e.what() << '\n';
        ++nbEchecs;
    }
    std::cout << (nbEchecs == 0 ? "Tous les tests passent\n" : std::to_string(nbEchecs) + " test(s) en échec\n");
    return nbEchecs == 0 ? 0 : 1;
}
