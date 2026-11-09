// test_pipeline.cpp
// Construction et exécution d'un pipeline à partir du script JSON (core/PipelineBuilder, Step::importConfig).
//
// GoogleTest, ctest -L unit : registre des steps, validation du JSON (options, types, enchaînement des entrées),
// aller-retour exportConfig → buildPipeline, step qui lève une exception, puis un pipeline réel import →
// phase-correlation sur deux tuiles 16 bits générées et découpées à une position entière connue.

#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
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
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace FileSystemNs = std::filesystem;

namespace StitchingNs::TestsNs {

constexpr double FRACTIONAL_POINT_COUNT = 2.5;
constexpr int PHASE_PARAMETER_COUNT = 15;
constexpr std::size_t REGISTERED_STEP_COUNT = 7;
constexpr int TEXTURE_SEED = 12345;
constexpr double PIXEL_MINIMUM = 2000.0;
constexpr double PIXEL_MAXIMUM = 60000.0;
constexpr int PATCH_SIZE = 10;
constexpr int MODIFIED_PIXEL = 1000;

using Json = nlohmann::json;

constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int TILE_OFFSET_X = 320;
constexpr double TEXTURE_SIGMA = 2.0;
constexpr double TRANSLATION_TOLERANCE = 0.05;
constexpr int EXPECTED_REGISTERED_STEP_COUNT = 3;
constexpr int EXPECTED_PIPELINE_STEP_COUNT = 2;
constexpr int EXPECTED_TILE_COUNT = 2;
constexpr int EXPECTED_MATCH_COUNT = 1;
constexpr std::size_t FIRST_STEP_INDEX = 0;
constexpr std::size_t SECOND_STEP_INDEX = 1;
constexpr std::size_t FIRST_TILE_INDEX = 0;
constexpr std::size_t SECOND_TILE_INDEX = 1;
constexpr std::size_t FIRST_MATCH_INDEX = 0;
constexpr int EXPECTED_PHASE_CORRELATION_PARAMETER_COUNT = 12;
constexpr int INTEGER_OPTION_VALUE = 3;
constexpr int INVALID_STEPS_VALUE = 3;
constexpr double NON_INTEGER_OPTION_VALUE = 2.5;
constexpr int PIXEL_ORIGIN = 0;
constexpr double ZERO_OVERLAP = 0.0;
constexpr unsigned int RANDOM_SEED = 12345;
constexpr double RANDOM_VALUE_MINIMUM = 0.0;
constexpr double RANDOM_VALUE_MAXIMUM = 1.0;
constexpr double NORMALIZED_VALUE_MINIMUM = 2000.0;
constexpr double NORMALIZED_VALUE_MAXIMUM = 60000.0;
constexpr int MODIFIED_PIXEL_REGION_SIZE = 10;
constexpr int MODIFIED_PIXEL_VALUE = 1000;

// Returns true if the callable throws std::invalid_argument containing the expected text.
template <class F>
bool throwsInvalidArgumentContaining(F callable, const std::string& expectedText) {
    try {
        callable();
    } catch (const std::invalid_argument& exception) {
        const bool CONTAINS_EXPECTED_TEXT = std::string(exception.what()).find(expectedText) != std::string::npos;
        if (!CONTAINS_EXPECTED_TEXT) std::cout << "        message reçu : " << exception.what() << '\n';
        return CONTAINS_EXPECTED_TEXT;
    }
    return false;
}

Json makeStepJson(const std::string& stepName, Json parameters = Json::array()) {
    return {{"name", stepName}, {"parameters", std::move(parameters)}};
}

Json makeScriptJson(const std::vector<Json>& steps) {
    return {{"pipeline", {{"steps", steps}}}};
}

// Step de test qui lève une exception à l'exécution
class StepQuiLeveClass : public StepNs::StepClass {
public:
    StepQuiLeveClass() : StepNs::StepClass(Json{{"name", "leve"}, {"parameters", Json::array()}}) {}
    bool execute() override { throw std::runtime_error("boum"); }
};

void testerRegistre() {
    std::cout << "Registre des steps\n";
    const auto noms = PipelineNs::getRegisteredStepNames();
    verifier(noms.size() == REGISTERED_STEP_COUNT, "7 steps enregistrés");
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

void testerExceptionDansUnStep() {
    std::cout << "Step qui lève une exception\n";
    PipelineNs::PipelineClass p;
    p.addStep(std::make_unique<StepQuiLeveClass>());
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

void testerPipelineReel() {
    std::cout << "Pipeline import -> phase-correlation sur deux tuiles 16 bits\n";
    const fs::path dossier = fs::temp_directory_path() / "kikirpa_test_pipeline";
    creerTuiles(dossier);

    PipelineNs::PipelineClass p = PipelineNs::buildPipeline(scriptReel(dossier));
    std::string journal;
    const bool ok = p.run([&](const std::string& m) { journal += "        " + m + "\n"; });
    verifier(ok, "run() réussit");
    if (!ok) {
        std::cout << journal;
        return;
    }
    EXPECT_TRUE(throwsInvalidArgumentContaining([] { PipelineNs::createStep("unknown"); }, "unknown step 'unknown'"));
}

TEST(PipelineBuilder, CompletePipelineJson) {
    const PipelineNs::PipelineClass PIPELINE = PipelineNs::buildPipelineFromFile(STITCHING_PIPELINE_JSON_PATH);
    ASSERT_EQ(PIPELINE.getSteps().size(), EXPECTED_PIPELINE_STEP_COUNT);
    EXPECT_EQ(PIPELINE.getSteps()[FIRST_STEP_INDEX]->getConfigJson().at("name"), "import");
    EXPECT_EQ(PIPELINE.getSteps()[SECOND_STEP_INDEX]->getConfigJson().at("name"), "phase-correlation");
}

TEST(PipelineBuilder, RejectsInvalidScriptStructure) {
    EXPECT_TRUE(throwsInvalidArgumentContaining([] { PipelineNs::buildPipeline(Json::array()); }, "JSON object"));
    EXPECT_TRUE(throwsInvalidArgumentContaining(
        [] { PipelineNs::buildPipeline({{"pipeline", {{"steps", INVALID_STEPS_VALUE}}}}); }, "pipeline.steps"));
    EXPECT_TRUE(throwsInvalidArgumentContaining(
        [] { PipelineNs::buildPipeline(makeScriptJson({{{"parameters", Json::array()}}})); }, "string 'name'"));
}

TEST(PipelineBuilder, RejectsInvalidParameters) {
    const Json UNKNOWN_PARAMETER_SCRIPT = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("string", "unknown", "x")}))});
    EXPECT_TRUE(rejectsPipeline(UNKNOWN_PARAMETER_SCRIPT, "unknown parameter 'unknown'"));

    const Json INCORRECT_TYPE_SCRIPT = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("int", "path", INTEGER_OPTION_VALUE)}))});
    EXPECT_TRUE(rejectsPipeline(INCORRECT_TYPE_SCRIPT, "has type 'string', got 'int'"));

    const Json INCORRECT_VALUE_SCRIPT = makeScriptJson(
        {makeStepJson("import"), makeStepJson("phase-correlation", Json::array({
            makeParameterJson("int", "nbPoints", NON_INTEGER_OPTION_VALUE)}))});
    EXPECT_TRUE(rejectsPipeline(INCORRECT_VALUE_SCRIPT, "expects a value of type 'int'"));

    const Json DUPLICATE_PARAMETER_SCRIPT = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("string", "path", "first"),
                                             makeParameterJson("string", "path", "second")}))});
    EXPECT_TRUE(rejectsPipeline(DUPLICATE_PARAMETER_SCRIPT, "duplicate parameter 'path'"));
}

TEST(PipelineBuilder, RejectsUnproducedInputs) {
    const Json MISSING_TILES_SCRIPT = makeScriptJson({makeStepJson("phase-correlation")});
    EXPECT_TRUE(rejectsPipeline(MISSING_TILES_SCRIPT, "input 'tiles' is not produced"));

    const Json MISSING_POSITION_SCRIPT = makeScriptJson({makeStepJson("import"), makeStepJson("graphcut")});
    EXPECT_TRUE(rejectsPipeline(MISSING_POSITION_SCRIPT, "input 'positions' is not produced"));
}

    const auto& tuiles = p.getDataStore().get<DataNs::TileList>("tiles");
    verifier(tuiles.size() == 2, "2 tuiles importées");
    verifier(tuiles.size() == 2 && tuiles[0]->getRight() == tuiles[1].get(), "B est la voisine de droite de A");
    verifier(tuiles.size() == 2 && fs::exists(tuiles[0]->getTiff()), "TIFF pyramidal créé à l'import");

    const auto& paires = p.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    verifier(paires.size() == 1, "1 paire mesurée");
    if (paires.size() != 1) return;
    const DataNs::Matches& m = paires[0].featureMatches;
    verifier(std::abs(m.translationX - DECALAGE_B) < TOLERANCE && std::abs(m.translationY) < TOLERANCE,
             "translation (" + std::to_string(m.translationX) + ", " + std::to_string(m.translationY) + ") = (" +
                 std::to_string(DECALAGE_B) + ", 0)");
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
    EXPECT_EQ(path, "elsewhere/");
    EXPECT_EQ(format, StepsNs::ImportInput{}.format);
    EXPECT_EQ(PIPELINE.getSteps()[SECOND_STEP_INDEX]->getConfigJson().at("parameters").size(),
              EXPECTED_PHASE_CORRELATION_PARAMETER_COUNT);

    const PipelineNs::PipelineClass COPY = PipelineNs::buildPipeline(PipelineNs.exportConfig());
    EXPECT_EQ(COPY.exportConfig(), PIPELINE.exportConfig());
}

TEST(PipelineBuilder, ReportsStepException) {
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<ThrowingStepClass>());
    std::string logMessage;
    const bool SUCCEEDED = pipeline.run([&](const std::string& message) { logMessage = message; });
    EXPECT_FALSE(SUCCEEDED);
    EXPECT_NE(logMessage.find("expected test exception"), std::string::npos);
}

void createTiles(const FileSystemNs::path& directory) {
    FileSystemNs::remove_all(directory);
    FileSystemNs::create_directories(directory);
    cv::Mat source(TILE_HEIGHT, TILE_OFFSET_X + TILE_WIDTH, CV_32F);
    cv::RNG randomGenerator(RANDOM_SEED);
    randomGenerator.fill(source, cv::RNG::UNIFORM, RANDOM_VALUE_MINIMUM, RANDOM_VALUE_MAXIMUM);
    cv::GaussianBlur(source, source, cv::Size(), TEXTURE_SIGMA);
    cv::normalize(source, source, NORMALIZED_VALUE_MINIMUM, NORMALIZED_VALUE_MAXIMUM, cv::NORM_MINMAX);
    cv::Mat source16Bit;
    source.convertTo(source16Bit, CV_16U);
    cv::imwrite((directory / "img_C00_R00.tif").string(),
                source16Bit(cv::Rect(PIXEL_ORIGIN, PIXEL_ORIGIN, TILE_WIDTH, TILE_HEIGHT)));
    cv::imwrite((directory / "img_C01_R00.tif").string(),
                source16Bit(cv::Rect(TILE_OFFSET_X, PIXEL_ORIGIN, TILE_WIDTH, TILE_HEIGHT)));
}

Json makeRealPipelineScript(const FileSystemNs::path& directory) {
    const Json IMPORT_STEP = makeStepJson("import", Json::array({
        makeParameterJson("string", "path", directory.string())}));
    return makeScriptJson({IMPORT_STEP, makeStepJson("phase-correlation")});
}

void verifyRealPipelineOutputs(const PipelineNs::PipelineClass& pipeline) {
    const auto& tiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");
    ASSERT_EQ(tiles.size(), EXPECTED_TILE_COUNT);
    EXPECT_EQ(tiles[FIRST_TILE_INDEX]->getRight(), tiles[SECOND_TILE_INDEX].get());
    EXPECT_TRUE(FileSystemNs::exists(tiles[FIRST_TILE_INDEX]->getTiff()));

    const auto& matches = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(matches.size(), EXPECTED_MATCH_COUNT);
    const DataNs::Matches& translation = matches[FIRST_MATCH_INDEX].featureMatches;
    EXPECT_NEAR(translation.translationX, TILE_OFFSET_X, TRANSLATION_TOLERANCE);
    EXPECT_NEAR(translation.translationY, PIXEL_ORIGIN, TRANSLATION_TOLERANCE);
}

TEST(PipelineBuilder, RunsRealPipeline) {
    const FileSystemNs::path DIRECTORY = FileSystemNs::temp_directory_path() / "kikirpa_test_pipeline";
    createTiles(DIRECTORY);
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(makeRealPipelineScript(DIRECTORY));
    ASSERT_TRUE(pipeline.run());
    verifyRealPipelineOutputs(pipeline);
    FileSystemNs::remove_all(DIRECTORY);
}

void verifyCheckpointFiles(const FileSystemNs::path& checkpointDirectory) {
    EXPECT_TRUE(FileSystemNs::exists(checkpointDirectory / "00_import" / "tiles.json"));
    EXPECT_TRUE(FileSystemNs::exists(checkpointDirectory / "01_phase-correlation" / "matches.json"));
}

void verifyRestoredPipelineOutputs(const PipelineNs::PipelineClass& pipeline, float expectedTranslationX) {
    const auto& tiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");
    ASSERT_EQ(tiles.size(), EXPECTED_TILE_COUNT);
    EXPECT_EQ(tiles[FIRST_TILE_INDEX]->getRight(), tiles[SECOND_TILE_INDEX].get());
    EXPECT_EQ(tiles[SECOND_TILE_INDEX]->getLeft(), tiles[FIRST_TILE_INDEX].get());

    const auto& matches = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(matches.size(), EXPECTED_MATCH_COUNT);
    EXPECT_EQ(matches[FIRST_MATCH_INDEX].featureMatches.translationX, expectedTranslationX);
    EXPECT_EQ(matches[FIRST_MATCH_INDEX].direction, DataNs::Direction::Right);
}

void modifySourceTile(const FileSystemNs::path& directory) {
    const FileSystemNs::path SOURCE_PATH = directory / "img_C00_R00.tif";
    cv::Mat image = cv::imread(SOURCE_PATH.string(), cv::IMREAD_UNCHANGED);
    image(cv::Rect(PIXEL_ORIGIN, PIXEL_ORIGIN, MODIFIED_PIXEL_REGION_SIZE, MODIFIED_PIXEL_REGION_SIZE))
        .setTo(cv::Scalar(MODIFIED_PIXEL_VALUE));
    cv::imwrite(SOURCE_PATH.string(), image);
}

PipelineNs::PipelineClass makeCheckpointedPipeline(const FileSystemNs::path& directory,
                                                    const FileSystemNs::path& checkpointDirectory) {
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(makeRealPipelineScript(directory));
    PipelineNs::RunState runState;
    runState.checkpointDirectory = checkpointDirectory.string();
    pipeline.setRunState(runState);
    return pipeline;
}

void verifySourceChangeRebuildsPipeline(PipelineNs::PipelineClass& pipeline, const FileSystemNs::path& directory) {
    modifySourceTile(directory);
    std::string log;
    const auto LOGGER = [&](const std::string& message) { log += message + "\n"; };
    ASSERT_TRUE(pipeline.run(LOGGER));
    EXPECT_NE(log.find("Step 1/2 OK"), std::string::npos);
    EXPECT_NE(log.find("Step 2/2 OK"), std::string::npos);
}

TEST(PipelineBuilder, ResumesFromCheckpointsAndDetectsChangedInputs) {
    const FileSystemNs::path DIRECTORY = FileSystemNs::temp_directory_path() / "kikirpa_test_pipeline_resume";
    const FileSystemNs::path CHECKPOINT_DIRECTORY = DIRECTORY / "checkpoints";
    createTiles(DIRECTORY);

    PipelineNs::PipelineClass pipeline = makeCheckpointedPipeline(DIRECTORY, CHECKPOINT_DIRECTORY);
    std::string log;
    const auto LOGGER = [&](const std::string& message) { log += message + "\n"; };
    ASSERT_TRUE(pipeline.run(LOGGER));
    verifyCheckpointFiles(CHECKPOINT_DIRECTORY);
    const auto& matches = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    const float EXPECTED_TRANSLATION_X = matches.at(FIRST_MATCH_INDEX).featureMatches.translationX;

    log.clear();
    ASSERT_TRUE(pipeline.run(LOGGER));
    EXPECT_NE(log.find("nothing to do"), std::string::npos);
    verifyRestoredPipelineOutputs(pipeline, EXPECTED_TRANSLATION_X);

    verifySourceChangeRebuildsPipeline(pipeline, directory);
    FileSystemNs::remove_all(directory);
}

} // namespace StitchingNs::TestsNs
