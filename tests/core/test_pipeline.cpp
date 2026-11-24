// tests/core/test_pipeline.cpp
// Construction et exécution d'un pipeline à partir du script JSON (core/PipelineBuilder, Step::importConfig).
//
// GoogleTest, ctest -L unit : registre des steps, validation du JSON (options, types, enchaînement des entrées),
// aller-retour exportConfig → buildPipeline, step qui lève une exception, puis un pipeline réel import →
// phase-correlation sur deux tuiles 16 bits générées et découpées à une position entière connue.

#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/pipeline_report.h"
#include "core/pipeline/run_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"
#include "steps/import_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json_fwd.hpp>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace fs = std::filesystem;

namespace StitchingNs::TestsNs {

constexpr double FRACTIONAL_POINT_COUNT = 2.5;
constexpr int PHASE_PARAMETER_COUNT = 17;
constexpr int TEXTURE_SEED = 12345;
constexpr double PIXEL_MINIMUM = 2000.0;
constexpr double PIXEL_MAXIMUM = 60000.0;
constexpr int PATCH_SIZE = 10;
constexpr int MODIFIED_PIXEL = 1000;
constexpr int MOSAIC_REPORT_WIDTH = 640;
constexpr int MOSAIC_REPORT_HEIGHT = 480;

using Json = nlohmann::json;

Json readJsonFile(const fs::path& path);

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

class SuccessfulStepClass : public StepNs::StepClass {
public:
    SuccessfulStepClass() : StepNs::StepClass(Json{{"name", "successful"}, {"parameters", Json::array()}}) {}
    bool execute() override {
        result = {{"status", "completed"}};
        return true;
    }
};

class UnreachedStepClass : public StepNs::StepClass {
public:
    UnreachedStepClass() : StepNs::StepClass(Json{{"name", "unreached"}, {"parameters", Json::array()}}) {}
    bool execute() override { return true; }
};

class MosaicOutputStepClass : public StepNs::StepClass {
public:
    MosaicOutputStepClass()
        : StepNs::StepClass(Json{
              {"name", "fusion"},
              {"parameters", Json::array()},
              {"outputs", Json::array({{{"dataType", "Mosaic"}, {"key", "mosaic"}}})}
          }) {}
    bool execute() override { return true; }
};

class ReportDataStepClass : public StepNs::StepClass {
public:
    ReportDataStepClass()
        : StepNs::StepClass(Json{
              {"name", "report-data"},
              {"parameters", Json::array()},
              {"outputs", Json::array({
                  {{"dataType", "PairResiduals"}, {"key", "pairResiduals"}},
                  {{"dataType", "OverlapEstimates"}, {"key", "overlapEstimates"}},
                  {{"dataType", "UnregisteredReportType"}, {"key", "unknown"}}
              })}
          }) {}

    bool execute() override {
        dataStore->add("pairResiduals", std::vector<DataNs::PairResidual>{
            {1, 2, cv::Point2d(1.5, -2.0), 2.5}
        });
        DataNs::PairOverlapEstimate overlap;
        overlap.tileIdA = 1;
        overlap.tileIdB = 2;
        overlap.direction = DataNs::Direction::Right;
        overlap.estimate.translationBInA = cv::Point2d(120.0, 3.0);
        overlap.estimate.overlapFraction = 0.2F;
        overlap.estimate.peakRatio = 3.5;
        overlap.commonAreaInA = cv::Rect(120, 0, 80, 300);
        overlap.commonAreaInB = cv::Rect(0, 0, 80, 300);
        dataStore->add("overlapEstimates", DataNs::OverlapEstimates{overlap});
        dataStore->add("unknown", std::vector<int>{1, 2, 3});
        result = {{"status", "completed"}};
        return true;
    }
};

class ReportCompactResultStepClass : public StepNs::StepClass {
public:
    ReportCompactResultStepClass()
        : StepNs::StepClass(Json{
              {"name", "phase-correlation"},
              {"parameters", Json::array({
                  {{"valueType", "int"}, {"name", "level"}, {"value", 2}}
              })},
              {"inputs", Json::array({
                  {{"dataType", "Tiles"}, {"key", "tiles"}}
              })},
              {"outputs", Json::array({
                  {{"dataType", "PairMatches"}, {"key", "matches"}}
              })}
          }) {
        Json points = Json::array();
        for (int pointIndex = 0; pointIndex < 1000; ++pointIndex)
            points.push_back({
                {"a", {pointIndex, pointIndex + 1}},
                {"b", {pointIndex + 2, pointIndex + 3}},
                {"ncc", 0.9}
            });
        result = {
            {"pairs", 1},
            {"confidence", Json::array({
                {
                    {"idA", 4},
                    {"idB", 9},
                    {"ncc", 0.92},
                    {"points", std::move(points)}
                }
            })}
        };
    }

    bool execute() override { return true; }
};

class ReportLifecycleStepClass : public StepNs::StepClass {
public:
    ReportLifecycleStepClass(fs::path reportPath, fs::path summaryPath)
        : StepNs::StepClass(Json{{"name", "report-lifecycle"}, {"parameters", Json::array()}}),
          reportPath(std::move(reportPath)), summaryPath(std::move(summaryPath)) {}

    bool execute() override {
        if (fs::exists(reportPath) || !fs::exists(summaryPath)) return false;
        const Json summary = readJsonFile(summaryPath);
        return summary.at("metadata").at("status") == "running" &&
               summary.at("progress").at("totalSteps") == 1;
    }

private:
    fs::path reportPath;
    fs::path summaryPath;
};

void testerRapportTypesEtResume() {
    const fs::path directory = fs::temp_directory_path() / "kikirpa_test_report_types";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const fs::path reportPath = directory / "execution.json";

    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<ReportDataStepClass>());
    pipeline.setReportPath(reportPath);
    pipeline.setReportMetadata({{"projectName", "test"}, {"executionId", "report-types"}});
    ASSERT_TRUE(pipeline.run());

    const Json report = readJsonFile(reportPath);
    const Json& residual = report.at("steps").at(0).at("outputs").at(0).at("value").at(0);
    EXPECT_EQ(residual.at("tileIdA"), 1);
    EXPECT_DOUBLE_EQ(residual.at("residualNorm").get<double>(), 2.5);
    const Json& overlap = report.at("steps").at(0).at("outputs").at(1).at("value").at(0);
    EXPECT_DOUBLE_EQ(overlap.at("translation").at(0).get<double>(), 120.0);
    EXPECT_NEAR(overlap.at("overlapFraction").get<double>(), 0.2, 1.0e-6);
    const Json& unsupported = report.at("steps").at(0).at("outputs").at(2);
    EXPECT_TRUE(unsupported.at("value").is_null());
    EXPECT_NE(unsupported.at("note").get<std::string>().find("unsupported data type"),
              std::string::npos);
    const Json summary = readJsonFile(directory / "execution-summary.json");
    EXPECT_EQ(summary.at("metadata").at("status"), "completed");
    EXPECT_EQ(summary.at("progress").at("completedSteps"), 1);
    EXPECT_EQ(summary.at("progress").at("totalSteps"), 1);
    EXPECT_FALSE(summary.contains("steps"));
    fs::remove_all(directory);
}

void testerRapportCompletSeulementALaFin() {
    const fs::path directory = fs::temp_directory_path() / "kikirpa_test_report_lifecycle";
    fs::remove_all(directory);
    const fs::path reportPath = directory / "execution.json";
    const fs::path summaryPath = directory / "execution-summary.json";

    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<ReportLifecycleStepClass>(reportPath, summaryPath));
    pipeline.setReportPath(reportPath);
    pipeline.setReportMetadata({{"projectName", "test"}, {"executionId", "lifecycle"}});
    ASSERT_TRUE(pipeline.run());
    EXPECT_TRUE(fs::exists(reportPath));
    EXPECT_EQ(readJsonFile(summaryPath).at("metadata").at("status"), "completed");
    fs::remove_all(directory);
}

void testerRapportNonBloquant() {
    const fs::path blocker = fs::temp_directory_path() / "kikirpa_report_write_blocker";
    fs::remove_all(blocker);
    {
        std::ofstream file(blocker);
        ASSERT_TRUE(file.good());
        file << "not a directory";
    }

    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<SuccessfulStepClass>());
    pipeline.setReportPath(blocker / "execution.json");
    std::string journal;
    EXPECT_TRUE(pipeline.run([&](const std::string& message) { journal += message + "\n"; }));
    EXPECT_NE(journal.find("warning: cannot"), std::string::npos);
    fs::remove(blocker);
}

void testerRapportArchiveSansResumeListe() {
    const fs::path directory = fs::temp_directory_path() / "kikirpa_test_summary_write_failure";
    fs::remove_all(directory);
    fs::create_directories(directory / "execution-summary.json");

    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<SuccessfulStepClass>());
    pipeline.setReportPath(directory / "execution.json");
    std::string journal;
    EXPECT_TRUE(pipeline.run([&](const std::string& message) { journal += message + "\n"; }));
    EXPECT_NE(journal.find("cannot update execution summary"), std::string::npos);
    EXPECT_EQ(readJsonFile(directory / "execution.json").at("metadata").at("status"), "completed");
    fs::remove_all(directory);
}

void testerRapportPause() {
    const fs::path directory = fs::temp_directory_path() / "kikirpa_test_report_pause";
    fs::remove_all(directory);
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<SuccessfulStepClass>());
    pipeline.setReportPath(directory / "execution.json");
    pipeline.setReportMetadata({{"projectName", "test"}, {"executionId", "paused"}});
    pipeline.requestPause();
    EXPECT_FALSE(pipeline.run());
    const Json report = readJsonFile(directory / "execution.json");
    EXPECT_EQ(report.at("metadata").at("status"), "paused");
    EXPECT_EQ(report.at("steps").at(0).at("status"), "notReached");
    EXPECT_FALSE(report.at("steps").at(0).at("configuration").contains("inputs"));
    EXPECT_FALSE(report.at("steps").at(0).at("configuration").contains("outputs"));
    EXPECT_NE(report.at("steps").at(0).at("reason").get<std::string>().find("arrêtée"),
              std::string::npos);
    fs::remove_all(directory);
}

void testerRapportMosaic() {
    StepNs::DataStoreClass dataStore;
    dataStore.add("mosaic", DataNs::Mosaic{
        fs::path("mosaic_image.tif"),
        fs::path("mosaic_provenance.tif"),
        fs::path("mosaic_mask.tif"),
        cv::Size(MOSAIC_REPORT_WIDTH, MOSAIC_REPORT_HEIGHT)
    });

    const MosaicOutputStepClass step;
    const Json report = PipelineNs::createStepReport(0, step, dataStore, Json::array());
    const Json& output = report.at("outputs").at(0).at("value");
    EXPECT_EQ(output.at("width"), MOSAIC_REPORT_WIDTH);
    EXPECT_EQ(output.at("height"), MOSAIC_REPORT_HEIGHT);
    EXPECT_EQ(output.at("imagePath"), "mosaic_image.tif");
    EXPECT_NE(report.at("summary").get<std::string>().find(
                  std::to_string(MOSAIC_REPORT_WIDTH) + " × " + std::to_string(MOSAIC_REPORT_HEIGHT)),
              std::string::npos);
}

void testerRegistre() {
    std::cout << "Registre des steps\n";
    const std::set<std::string> attendus = {
        "absolute-pos", "dynamic-programming-dijkstra", "export", "final-verification", "fusion",
        "graphcut", "import", "intensity-correction", "lightglue", "manual-matches", "overlap-estimation",
        "phase-correlation", "sift", "vote-2d"};
    const auto noms = PipelineNs::getRegisteredStepNames();
    const std::set<std::string> enregistres(noms.begin(), noms.end());

    for (const auto& nom : enregistres)
        verifier(attendus.count(nom) == 1, "step '" + nom + "' presente dans le registre mais pas attendu");
    for (const auto& nom : attendus)
        verifier(enregistres.count(nom) == 1, "step '" + nom + "' attendu mais absent du registre");

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
    verifier(p.getSteps()[1]->getConfigJson().at("parameters").size() == PHASE_PARAMETER_COUNT, "phase-correlation garde ses 17 options");

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
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<SuccessfulStepClass>());
    pipeline.addStep(std::make_unique<StepQuiLeveClass>());
    pipeline.setReportPath(reportPath);
    pipeline.setReportMetadata({{"projectName", "projet <&\""}, {"executionId", "failure-case"}});
    pipeline.addStep(std::make_unique<UnreachedStepClass>());
    const bool succeeded = pipeline.run([&](const std::string& logMessage) { message = logMessage; });
    verifier(!succeeded, "run() renvoie false");
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
    verifier(reportHtml.find("projet &lt;&amp;&quot;") != std::string::npos,
             "l'export HTML échappe les caractères spéciaux du nom de projet");
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
    const auto& tiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");
    verifier(tiles.size() == 2, "2 tuiles importées");
    verifier(tiles.size() == 2 && tiles[0]->getRight() == tiles[1].get(), "B est la voisine de droite de A");
    verifier(tiles.size() == 2 && fs::exists(tiles[0]->getTiff()), "TIFF pyramidal créé à l'import");

    const auto& pairMatches = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    verifier(pairMatches.size() == 1, "1 paire mesurée");
    if (pairMatches.size() != 1) return 0.0F;
    const DataNs::Matches& matches = pairMatches[0].featureMatches;
    verifier(std::abs(matches.translationX - DECALAGE_B) < TOLERANCE &&
                 std::abs(matches.translationY) < TOLERANCE,
             "translation (" + std::to_string(matches.translationX) + ", " +
                 std::to_string(matches.translationY) + ") = (" +
                 std::to_string(DECALAGE_B) + ", 0)");
    return matches.translationX;
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
    verifier(report.at("steps").at(1).at("inputs").at(0).at("available") == true &&
                 !report.at("steps").at(1).at("inputs").at(0).contains("value"),
             "les entrées sont référencées sans recopier la sortie précédente");
    const Json& match = report.at("steps").at(1).at("outputs").at(0).at("value").at(0);
    verifier(match.at("translationX").get<double>() == expectedTranslation,
             "le rapport contient la translation mesurée");
    verifier(match.contains("correspondenceCount") && match.contains("confidenceCount") &&
                 !match.contains("pointsInA") && !match.contains("pointsInB"),
             "le rapport résume les correspondances sans dupliquer tous les points");
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

void testerRapportJusquaAbsolutePos() {
    const fs::path directory = fs::temp_directory_path() / "kikirpa_test_report_absolute_pos";
    creerTuiles(directory);
    const Json pipelineScript = script({
        etape("import", {{{"valueType", "string"}, {"name", "path"}, {"value", directory.string()}}}),
        etape("phase-correlation"),
        etape("absolute-pos")
    });
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(pipelineScript);
    const fs::path reportPath = directory / "execution.json";
    pipeline.setReportPath(reportPath);
    pipeline.setReportMetadata({{"projectName", "absolute-pos-test"}, {"executionId", "absolute-pos"}});
    std::string journal;
    ASSERT_TRUE(pipeline.run([&](const std::string& message) { journal += message + "\n"; })) << journal;
    const Json report = readJsonFile(reportPath);
    ASSERT_EQ(report.at("steps").size(), 3);
    EXPECT_EQ(report.at("steps").at(2).at("status"), "completed");
    EXPECT_EQ(report.at("steps").at(2).at("outputs").at(1).at("dataType"), "PairResiduals");
    EXPECT_TRUE(report.at("steps").at(2).at("outputs").at(1).at("value").is_array());
    fs::remove_all(directory);
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
    p.setReportPath(dossier / "execution.json");
    p.setReportMetadata({{"projectName", "reprise-test"}, {"executionId", "reprise"}});
    std::string journal;
    const auto log = [&](const std::string& m) { journal += m + "\n"; };
    verifier(p.run(log), "run 1 réussi");
    verifier(fs::exists(checkpoints / "00_import" / "tiles.json") && fs::exists(checkpoints / "01_phase-correlation" / "matches.json"),
             "sorties des deux steps écrites");
    const float dx1 = p.getDataStore().get<std::vector<DataNs::PairMatches>>("matches").at(0).featureMatches.translationX;

    verifierReprise(p, journal, dx1);
    const Json resumedReport = readJsonFile(dossier / "execution.json");
    verifier(resumedReport.at("steps").at(0).at("status") == "skipped" &&
                 resumedReport.at("steps").at(1).at("status") == "skipped",
             "les étapes restaurées sont marquées comme réutilisées dans le rapport");
    verifier(!resumedReport.at("steps").at(0).at("configuration").contains("inputs") &&
                 !resumedReport.at("steps").at(1).at("configuration").contains("outputs"),
             "les étapes réutilisées ne répètent pas les descripteurs de données");

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
TEST(PipelineBuilder, RapportJusquaAbsolutePos) { testerRapportJusquaAbsolutePos(); }
TEST(PipelineBuilder, RepriseReelle) { testerRepriseReelle(); }
TEST(PipelineReport, SerialisePairResidualsEtOverlapEstimates) { testerRapportTypesEtResume(); }
TEST(PipelineReport, EcritArchiveCompleteSeulementALaFin) { testerRapportCompletSeulementALaFin(); }
TEST(PipelineReport, EchecEcritureNonBloquant) { testerRapportNonBloquant(); }
TEST(PipelineReport, ArchiveSansResumeListe) { testerRapportArchiveSansResumeListe(); }
TEST(PipelineReport, Pause) { testerRapportPause(); }
TEST(PipelineReport, SerialiseMosaic) { testerRapportMosaic(); }
TEST(PipelineReport, ResumeLesPointsDeConfianceSansCopierLeursCoordonnees) {
    StepNs::DataStoreClass dataStore;
    const ReportCompactResultStepClass step;
    const Json report = PipelineNs::createStepReport(
        0, step, dataStore, PipelineNs::createStepInputs(step, dataStore));

    const Json expectedConfiguration = {{"parameters", Json::array({
        {{"valueType", "int"}, {"name", "level"}, {"value", 2}}
    })}};
    EXPECT_EQ(report.at("configuration"), expectedConfiguration);
    EXPECT_FALSE(report.at("configuration").contains("inputs"));
    EXPECT_FALSE(report.at("configuration").contains("outputs"));
    EXPECT_EQ(report.at("inputs").at(0).at("available"), false);
    EXPECT_EQ(report.at("outputs").at(0).at("key"), "matches");

    const Json& confidence = report.at("result").at("confidence").at(0);
    EXPECT_EQ(confidence.at("pointCount"), 1000);
    EXPECT_EQ(confidence.at("ncc"), 0.92);
    EXPECT_FALSE(confidence.contains("points"));

    const fs::path htmlPath = fs::temp_directory_path() / "kikirpa_compact_report.html";
    const Json archive = {
        {"metadata", {{"projectName", "test"}, {"executionId", "compact"}}},
        {"steps", Json::array({report})}
    };
    PipelineNs::writeHtmlReport(htmlPath, archive);
    const std::string html = readTextFile(htmlPath);
    EXPECT_NE(html.find("pointCount"), std::string::npos);
    EXPECT_EQ(html.find("\"points\""), std::string::npos);
    fs::remove(htmlPath);
}

}
