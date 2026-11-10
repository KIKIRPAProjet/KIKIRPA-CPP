// tests/core/test_pipeline.cpp
// Construction et exécution d'un pipeline à partir du script JSON (core/PipelineBuilder, Step::importConfig).
//
// GoogleTest, ctest -L unit : registre des steps, validation du JSON (options, types, enchaînement des entrées),
// aller-retour exportConfig → buildPipeline, step qui lève une exception, puis un pipeline réel import →
// phase-correlation sur deux tuiles 16 bits générées et découpées à une position entière connue.

#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/pipeline_report.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/step.h"
#include "core/pipeline/run_state.h"
#include "steps/import_step.h"
#include <gtest/gtest.h>
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace StitchingNs::TestsNs {

constexpr double FRACTIONAL_POINT_COUNT = 2.5;
constexpr int PHASE_PARAMETER_COUNT = 15;
constexpr std::size_t REGISTERED_STEP_COUNT = 9;
constexpr int TEXTURE_SEED = 12345;
constexpr double PIXEL_MINIMUM = 2000.0;
constexpr double PIXEL_MAXIMUM = 60000.0;
constexpr int PATCH_SIZE = 10;
constexpr int MODIFIED_PIXEL = 1000;

using Json = nlohmann::json;

constexpr int    LARGEUR_TUILE  = 400;
constexpr int    HAUTEUR_TUILE  = 300;
constexpr int    DECALAGE_B     = 320;    // px : coin de B dans A (recouvrement de 20 %)
constexpr double SIGMA_TEXTURE  = 2.0;    // flou de la texture aléatoire
constexpr double TOLERANCE      = 0.05;   // px

// Vérification non bloquante : en cas d'échec, GoogleTest affiche le message
void verifier(bool ok, const std::string& message) {
    EXPECT_TRUE(ok) << message;
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
    return {{"name", nom}, {"parameters", std::move(options)}};
}

// std::vector et non Json : avec nlohmann, {x} à un seul élément ne donne pas un tableau
Json script(const std::vector<Json>& etapes) {
    return {{"pipeline", {{"steps", etapes}}}};
}

// Step de test qui lève une exception à l'exécution
class StepQuiLeveClass : public StepNs::StepClass {
public:
    StepQuiLeveClass() : StepNs::StepClass(Json{{"name", "leve"}, {"parameters", Json::array()}}) {}
    bool execute() override { throw std::runtime_error("boum"); }
};

class StepReussiClass : public StepNs::StepClass {
public:
    StepReussiClass() : StepNs::StepClass(Json{{"name", "reussi"}, {"parameters", Json::array()}}) {}
    bool execute() override {
        result = {{"status", "completed"}};
        return true;
    }
};

class StepNonAtteintClass : public StepNs::StepClass {
public:
    StepNonAtteintClass() : StepNs::StepClass(Json{{"name", "non-atteint"}, {"parameters", Json::array()}}) {}
    bool execute() override { return true; }
};

void testerRegistre() {
    std::cout << "Registre des steps\n";
    const auto noms = PipelineNs::getRegisteredStepNames();
    verifier(noms.size() == REGISTERED_STEP_COUNT, "9 steps enregistrés");
    for (const auto& nom : noms) {
        const auto step = PipelineNs::createStep(nom);
        verifier(step && step->getConfigJson().value("name", "") == nom, "createStep(\"" + nom + "\") a le bon nom");
    }
    verifier(leveInvalidArgument([] { PipelineNs::createStep("inconnu"); }, "unknown step 'inconnu'"), "step inconnu refusé");
}

void testerDefaults() {
    std::cout << "Fusion des options\n";
    const PipelineNs::PipelineClass p = PipelineNs::buildPipeline(script({etape("import", {{{"valueType", "string"}, {"name", "path"}, {"value", "ailleurs/"}}}),
                                             etape("phase-correlation", {{{"valueType", "float"}, {"name", "overlap"}, {"value", 0}}})}));
    const Json& importCfg = p.getSteps()[0]->getConfigJson();
    std::string path, format;
    for (const Json& o : importCfg.at("parameters")) {
        if (o.at("name") == "path") path = o.at("value").get<std::string>();
        if (o.at("name") == "format") format = o.at("value").get<std::string>();
    }
    verifier(path == "ailleurs/", "option fournie : valeur remplacée");
    verifier(format == StepsNs::ImportInput{}.format, "option absente : valeur par défaut gardée");
    verifier(p.getSteps()[1]->getConfigJson().at("parameters").size() == PHASE_PARAMETER_COUNT, "phase-correlation garde ses 15 options");

    std::cout << "Aller-retour exportConfig -> buildPipeline\n";
    const PipelineNs::PipelineClass copie = PipelineNs::buildPipeline(p.exportConfig());
    verifier(copie.exportConfig() == p.exportConfig(), "même script après relecture");
}

void testerValidation() {
    std::cout << "Validation du script\n";
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(Json::array()); }, "JSON object"), "script qui n'est pas un objet");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline({{"pipeline", {{"steps", 3}}}}); }, "pipeline.steps"),
             "steps qui n'est pas un tableau");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({{{"parameters", Json::array()}}})); }, "string 'name'"),
             "step sans nom");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("import", {{{"valueType", "string"}, {"name", "inconnue"}, {"value", "x"}}})})); },
                                 "unknown parameter 'inconnue'"),
             "option inconnue");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("import", {{{"valueType", "int"}, {"name", "path"}, {"value", 3}}})})); },
                                 "has type 'string', got 'int'"),
             "type d'option différent du type déclaré");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("import"), etape("phase-correlation", {{{"valueType", "int"}, {"name", "nbPoints"}, {"value", FRACTIONAL_POINT_COUNT}}})})); },
                                 "expects a value of type 'int'"),
             "valeur réelle pour une option entière");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("import", {{{"valueType", "string"}, {"name", "path"}, {"value", "a"}}, {{"valueType", "string"}, {"name", "path"}, {"value", "b"}}})})); },
                                 "duplicate parameter 'path'"),
             "option en double");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("phase-correlation")})); },
                                 "input 'tiles' is not produced"),
             "entrée produite par aucun step précédent");
    verifier(leveInvalidArgument([] { PipelineNs::buildPipeline(script({etape("import"), etape("graphcut")})); },
                                 "input 'positions' is not produced"),
             "graphcut sans step qui produit les positions");


    testerDefaults();
}

Json readJsonFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input);
}

std::string readTextFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Json runFailingPipeline(const fs::path& reportPath, std::string& message) {
    PipelineNs::PipelineClass p;
    p.addStep(std::make_unique<StepReussiClass>());
    p.addStep(std::make_unique<StepQuiLeveClass>());
    p.setReportPath(reportPath);
    p.setReportMetadata({{"projectName", "test"}, {"executionId", "failure-case"}});
    p.addStep(std::make_unique<StepNonAtteintClass>());
    const bool ok = p.run([&](const std::string& m) { message = m; });
    verifier(!ok, "run() renvoie false");
    return readJsonFile(reportPath);
}

void verifyFailureReport(const Json& report, const fs::path& htmlPath) {
    verifier(report.at("metadata").at("status") == "failed", "le rapport indique l'échec global");
    verifier(report.at("metadata").contains("startedAt") && report.at("metadata").contains("finishedAt"),
             "le rapport contient les dates de début et de fin");
    verifier(report.at("steps").size() == 3, "le rapport contient toutes les étapes configurées");
    verifier(report.at("steps").at(0).at("status") == "completed", "le rapport conserve le step réussi");
    verifier(report.at("steps").at(1).at("status") == "failed" &&
                 report.at("steps").at(1).at("reason") == "boum",
             "le rapport documente l'étape en échec et son motif");
    verifier(report.at("steps").at(2).at("status") == "notReached",
             "le rapport distingue l'étape non atteinte");
    verifier(report.at("steps").at(2).at("reason").get<std::string>().find("arrêtée") != std::string::npos,
             "le rapport explique pourquoi l'étape n'a pas été atteinte");
    const std::string reportHtml = readTextFile(htmlPath);
    verifier(reportHtml.find("Motif : boum") != std::string::npos,
             "l'export HTML présente le motif d'échec");
}

void testerExceptionDansUnStep() {
    std::cout << "Step qui lève une exception\n";
    const fs::path reportPath = fs::temp_directory_path() / "kikirpa_test_pipeline_report.json";
    const fs::path htmlPath = fs::temp_directory_path() / "kikirpa_test_pipeline_report.html";
    fs::remove(reportPath);
    fs::remove(htmlPath);
    std::string message;
    const Json report = runFailingPipeline(reportPath, message);
    PipelineNs::writeHtmlReport(htmlPath, report);
    verifier(message.find("boum") != std::string::npos, "le message de l'exception est transmis au journal");
    verifyFailureReport(report, htmlPath);
    fs::remove(reportPath);
    fs::remove(htmlPath);
}

// Deux tuiles 16 bits img_C00_R00.tif et img_C01_R00.tif découpées dans une texture aléatoire floutée,
// B à DECALAGE_B px à droite de A : vérité exacte
void creerTuiles(const fs::path& dossier) {
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    cv::Mat source(HAUTEUR_TUILE, DECALAGE_B + LARGEUR_TUILE, CV_32F);
    cv::RNG rng(TEXTURE_SEED);
    rng.fill(source, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(source, source, cv::Size(), SIGMA_TEXTURE);
    cv::normalize(source, source, PIXEL_MINIMUM, PIXEL_MAXIMUM, cv::NORM_MINMAX);
    cv::Mat source16;
    source.convertTo(source16, CV_16U);
    cv::imwrite((dossier / "img_C00_R00.tif").string(), source16(cv::Rect(0, 0, LARGEUR_TUILE, HAUTEUR_TUILE)));
    cv::imwrite((dossier / "img_C01_R00.tif").string(), source16(cv::Rect(DECALAGE_B, 0, LARGEUR_TUILE, HAUTEUR_TUILE)));
}

Json scriptReel(const fs::path& dossier) {
    return script({etape("import", {{{"valueType", "string"}, {"name", "path"}, {"value", dossier.string()}}}),
                   etape("phase-correlation")});
}

void setRealPipelineReportMetadata(PipelineNs::PipelineClass& pipeline) {
    const Json projectMetadata = {
        {"grid", {
            {"rows", 1},
            {"columns", 2},
            {"tiles", Json::array({
                {{"row", 0}, {"column", 0}, {"path", "A.tif"}},
                {{"row", 0}, {"column", 1}, {"path", "B.tif"}}
            })}
        }}
    };
    pipeline.setReportMetadata({
        {"projectName", "projet-test"},
        {"executionId", "execution-reelle"},
        {"project", projectMetadata}
    });
}

float verifyRealPipelineOutputs(const PipelineNs::PipelineClass& pipeline) {
    const auto& tuiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2, "2 tuiles importées");
    verifier(tuiles.size() == 2 && tuiles[0]->getRight() == tuiles[1].get(), "B est la voisine de droite de A");
    verifier(tuiles.size() == 2 && fs::exists(tuiles[0]->getTiff()), "TIFF pyramidal créé à l'import");

    const auto& paires = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    verifier(paires.size() == 1, "1 paire mesurée");
    if (paires.size() != 1) return 0.0F;
    const DataNs::Matches& m = paires[0].featureMatches;
    verifier(std::abs(m.translationX - DECALAGE_B) < TOLERANCE && std::abs(m.translationY) < TOLERANCE,
             "translation (" + std::to_string(m.translationX) + ", " + std::to_string(m.translationY) + ") = (" +
                 std::to_string(DECALAGE_B) + ", 0)");
    return m.translationX;
}

void verifyRealReportData(const Json& report, float expectedTranslation) {
    verifier(report.at("metadata").at("status") == "completed",
             "le rapport enregistre le statut global réel");
    verifier(report.at("steps").size() == 2 &&
                 report.at("steps").at(0).at("status") == "completed" &&
                 report.at("steps").at(1).at("status") == "completed",
             "le rapport contient les étapes réellement exécutées");
    verifier(report.at("steps").at(0).at("outputs").at(0).at("value").size() == 2,
             "le rapport contient les tuiles importées");
    verifier(report.at("steps").at(1).at("outputs").at(0).at("value").at(0)
                 .at("translationX").get<double>() == expectedTranslation,
             "le rapport contient les mesures réelles de la sortie");
}

void verifyRealReportHtml(const Json& report, const fs::path& htmlPath) {
    PipelineNs::writeHtmlReport(htmlPath, report);
    const std::string reportHtml = readTextFile(htmlPath);
    verifier(reportHtml.find("phase-correlation") != std::string::npos, "l'HTML nomme le step d'appariement");
    verifier(reportHtml.find("Ordre d'import configuré") != std::string::npos &&
                 reportHtml.find("A.tif") != std::string::npos,
             "l'HTML restitue la grille et l'ordre configuré");
    verifier(reportHtml.find(std::to_string(LARGEUR_TUILE)) != std::string::npos,
             "l'HTML contient les dimensions réelles des tuiles importées");
}

void testerPipelineReel() {
    std::cout << "Pipeline import -> phase-correlation sur deux tuiles 16 bits\n";
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_pipeline";
    creerTuiles(dossier);
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(scriptReel(dossier));
    const fs::path reportPath = dossier / "execution.json";
    const fs::path htmlPath = dossier / "rapport.html";
    pipeline.setReportPath(reportPath);
    setRealPipelineReportMetadata(pipeline);
    std::string journal;
    const bool succeeded = pipeline.run([&](const std::string& message) { journal += message + "\n"; });
    verifier(succeeded, "run() réussit");
    if (succeeded) {
        const float translation = verifyRealPipelineOutputs(pipeline);
        const Json report = readJsonFile(reportPath);
        verifyRealReportData(report, translation);
        verifyRealReportHtml(report, htmlPath);
    } else {
        std::cout << journal;
    }
    fs::remove_all(dossier);
}

void verifierReprise(PipelineNs::PipelineClass& p, std::string& journal, float dx1) {
    const auto log = [&](const std::string& message) { journal += message + "\n"; };
    journal.clear();
    verifier(p.run(log), "run 2 réussi");
    verifier(journal.find("nothing to do") != std::string::npos, "run 2 : aucun step refait");
    const auto& tuiles = p.getDataStore().get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2 && tuiles[0]->getRight() == tuiles[1].get() && tuiles[1]->getLeft() == tuiles[0].get(),
             "tuiles et voisins restaurés");
    const auto& paires = p.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    verifier(paires.size() == 1 && paires[0].featureMatches.translationX == dx1 && paires[0].direction == DataNs::Direction::Right,
             "matches restaurés à l'identique");

}

void testerRepriseReelle() {
    std::cout << "Reprise du pipeline réel sur checkpoints\n";
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_pipeline_reprise";
    const fs::path checkpoints = dossier / "checkpoints";
    creerTuiles(dossier);

    PipelineNs::PipelineClass p = PipelineNs::buildPipeline(scriptReel(dossier));
    PipelineNs::RunState r;
    r.checkpointDirectory = checkpoints.string();
    p.setRunState(r);
    std::string journal;
    const auto log = [&](const std::string& m) { journal += m + "\n"; };
    verifier(p.run(log), "run 1 réussi");
    verifier(fs::exists(checkpoints / "00_import" / "tiles.json") && fs::exists(checkpoints / "01_phase-correlation" / "matches.json"),
             "sorties des deux steps écrites");
    const float dx1 = p.getDataStore().get<std::vector<DataNs::PairMatches>>("matches").at(0).featureMatches.translationX;

    verifierReprise(p, journal, dx1);

    // Une image source modifiée périme le checkpoint de l'import, donc de toute la suite
    {
        cv::Mat a = cv::imread((dossier / "img_C00_R00.tif").string(), cv::IMREAD_UNCHANGED);
        a(cv::Rect(0, 0, PATCH_SIZE, PATCH_SIZE)).setTo(cv::Scalar(MODIFIED_PIXEL));
        cv::imwrite((dossier / "img_C00_R00.tif").string(), a);
    }
    journal.clear();
    verifier(p.run(log), "run 3 réussi");
    verifier(journal.find("Step 1/2 OK") != std::string::npos && journal.find("Step 2/2 OK") != std::string::npos,
             "image modifiée : import et appariement refaits");
    fs::remove_all(dossier);
}

// Une exception inattendue fait échouer le test : GoogleTest l'attrape et affiche son message
TEST(PipelineBuilder, Registre) { testerRegistre(); }
TEST(PipelineBuilder, Validation) { testerValidation(); }
TEST(PipelineBuilder, ExceptionDansUnStep) { testerExceptionDansUnStep(); }
TEST(PipelineBuilder, PipelineReel) { testerPipelineReel(); }
TEST(PipelineBuilder, RepriseReelle) { testerRepriseReelle(); }

}
