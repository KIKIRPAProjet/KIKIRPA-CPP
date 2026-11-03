// test_pipeline.cpp
// Construction et exécution d'un pipeline à partir du script JSON (core/PipelineBuilder, Step::importConfig).
//
// GoogleTest, ctest -L unit : registre des steps, validation du JSON (options, types, enchaînement des entrées),
// aller-retour exportConfig → buildPipeline, step qui lève une exception, puis un pipeline réel import →
// phase-correlation sur deux tuiles 16 bits générées et découpées à une position entière connue.

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"
#include "steps/import_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

// Returns true if the callable throws std::invalid_argument containing the expect
template <class F>
bool throwsInvalidArgumentContaining(F callable, const std::string& expectedText) {
    try {
        fcallable();
    } catch (const std::invalid_argument& exception) {
        const bool containsExpectedText = std::string(exception.what()).find(expectedText) != std::string::npos;
        if (!containsExpectedText) std::cout << "        message reçu : " << exception.what() << '\n';
        return containsExpectedText;
    }
    return false;
}

Json makeStepJson(const std::string& stepName, Json parameters = Json::array()) {
    return {{"name", stepName}, {"parameters", std::move(parameters)}};
}

// std::vector et non Json : avec nlohmann, {x} à un seul élément ne donne pas un tableau
Json makeScriptJson(const std::vector<Json>& steps) {
    return {{"pipeline", {{"steps", steps}}}};
}

Json makeParameterJson(const std::string& valueType, const std::string& name, Json value) {
    return {{"valueType", valueType}, {"name", name}, {"value", std::move(value)}};
}

bool rejectsPipeline(const Json& scriptJson, const std::string& expectedText) {
    return throwsInvalidArgumentContaining([&] { PipelineNs::buildPipeline(scriptJson); }, expectedText);
}

class ThrowingStepClass : public StepNs::StepClass {
    public:
        ThrowingStepClass() : StepNs::StepClass(Json{{"name", "throwing-step"}, {"parameters", Json::array()}}) {}
        bool execute() override { throw std::runtime_error("expected test exception"); }
};

TEST(PipelineBuilder, StepRegistry) {
    const auto stepNames = PipelineNs::getRegisteredStepNames();
    EXPECT_EQ(stepNames.size(), EXPECTED_REGISTERED_STEP_COUNT);
    EXPECT_EQ(stepNames, (std::vector<std::string>{"graphcut", "import", "phase-correlation"}));
    for (const auto& stepName : stepNames) {
        const auto step = PipelineNs::createStep(stepName);
        ASSERT_NE(step, nullptr);
        EXPECT_EQ(step->getConfigJson().value("name", ""), stepName);
    }
    EXPECT_TRUE(throwsInvalidArgumentContaining([] { PipelineNs::createStep("unknown"); }, "unknown step 'unknown'"));
}

TEST(PipelineBuilder, CompletePipelineJson) {
    const PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(STITCHING_PIPELINE_JSON_PATH);
    ASSERT_EQ(pipeline.getSteps().size(), EXPECTED_PIPELINE_STEP_COUNT);
    EXPECT_EQ(pipeline.getSteps()[FIRST_STEP_INDEX]->getConfigJson().at("name"), "import");
    EXPECT_EQ(pipeline.getSteps()[SECOND_STEP_INDEX]->getConfigJson().at("name"), "phase-correlation");
}

TEST(PipelineBuilder, RejectsInvalidScriptStructure) {
    EXPECT_TRUE(throwsInvalidArgumentContaining([] { PipelineNs::buildPipeline(Json::array()); }, "JSON object"));
    EXPECT_TRUE(throwsInvalidArgumentContaining(
        [] { PipelineNs::buildPipeline({{"pipeline", {{"steps", INVALID_STEPS_VALUE}}}}); }, "pipeline.steps"));
    EXPECT_TRUE(throwsInvalidArgumentContaining(
        [] { PipelineNs::buildPipeline(makeScriptJson({{{"parameters", Json::array()}}})); }, "string 'name'"));
}

TEST(PipelineBuilder, RejectsInvalidParameters) {
    const Json unknownParameterScript = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("string", "unknown", "x")}))});
    EXPECT_TRUE(rejectsPipeline(unknownParameterScript, "unknown parameter 'unknown'"));

    const Json incorrectTypeScript = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("int", "path", INTEGER_OPTION_VALUE)}))});
    EXPECT_TRUE(rejectsPipeline(incorrectTypeScript, "has type 'string', got 'int'"));

    const Json incorrectValueScript = makeScriptJson(
        {makeStepJson("import"), makeStepJson("phase-correlation", Json::array({
            makeParameterJson("int", "nbPoints", NON_INTEGER_OPTION_VALUE)}))});
    EXPECT_TRUE(rejectsPipeline(incorrectValueScript, "expects a value of type 'int'"));

    const Json duplicateParameterScript = makeScriptJson(
        {makeStepJson("import", Json::array({makeParameterJson("string", "path", "first"),
                                             makeParameterJson("string", "path", "second")}))});
    EXPECT_TRUE(rejectsPipeline(duplicateParameterScript, "duplicate parameter 'path'"));
}

TEST(PipelineBuilder, RejectsUnproducedInputs) {
    const Json missingTilesScript = makeScriptJson({makeStepJson("phase-correlation")});
    EXPECT_TRUE(rejectsPipeline(missingTilesScript, "input 'tiles' is not produced"));

    const Json missingPositionsScript = makeScriptJson({makeStepJson("import"), makeStepJson("graphcut")});
    EXPECT_TRUE(rejectsPipeline(missingPositionsScript, "input 'positions' is not produced"));
}

TEST(PipelineBuilder, MergesOptionsAndRoundTripsConfiguration) {
    const PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(makeScriptJson({
        makeStepJson("import", Json::array({makeParameterJson("string", "path", "elsewhere/")})),
        makeStepJson("phase-correlation", Json::array({makeParameterJson("float", "overlap", ZERO_OVERLAP)}))}));
    const Json& importConfig = pipeline.getSteps()[FIRST_STEP_INDEX]->getConfigJson();
    std::string path;
    std::string format;
    for (const Json& parameter : importConfig.at("parameters")) {
        if (parameter.at("name") == "path") path = parameter.at("value").get<std::string>();
        if (parameter.at("name") == "format") format = parameter.at("value").get<std::string>();
    }
    EXPECT_EQ(path, "elsewhere/");
    EXPECT_EQ(format, StepsNs::ImportInput{}.format);
    EXPECT_EQ(pipeline.getSteps()[SECOND_STEP_INDEX]->getConfigJson().at("parameters").size(),
              EXPECTED_PHASE_CORRELATION_PARAMETER_COUNT);

    const PipelineNs::PipelineClass copy = PipelineNs::buildPipeline(pipeline.exportConfig());
    EXPECT_EQ(copy.exportConfig(), pipeline.exportConfig());
}

TEST(PipelineBuilder, ReportsStepException) {
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<ThrowingStepClass>());
    std::string logMessage;
    const bool succeeded = pipeline.run([&](const std::string& message) { logMessage = message; });
    EXPECT_FALSE(succeeded);
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
    const Json importStep = makeStepJson("import", Json::array({
        makeParameterJson("string", "path", directory.string())}));
    return makeScriptJson({importStep, makeStepJson("phase-correlation")});
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
    const FileSystemNs::path directory = FileSystemNs::temp_directory_path() / "kikirpa_test_pipeline";
    createTiles(directory);
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(makeRealPipelineScript(directory));
    ASSERT_TRUE(pipeline.run());
    verifyRealPipelineOutputs(pipeline);
    FileSystemNs::remove_all(directory);
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
    const FileSystemNs::path sourcePath = directory / "img_C00_R00.tif";
    cv::Mat image = cv::imread(sourcePath.string(), cv::IMREAD_UNCHANGED);
    image(cv::Rect(PIXEL_ORIGIN, PIXEL_ORIGIN, MODIFIED_PIXEL_REGION_SIZE, MODIFIED_PIXEL_REGION_SIZE))
        .setTo(cv::Scalar(MODIFIED_PIXEL_VALUE));
    cv::imwrite(sourcePath.string(), image);
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
    const auto logger = [&](const std::string& message) { log += message + "\n"; };
    ASSERT_TRUE(pipeline.run(logger));
    EXPECT_NE(log.find("Step 1/2 OK"), std::string::npos);
    EXPECT_NE(log.find("Step 2/2 OK"), std::string::npos);
}

TEST(PipelineBuilder, ResumesFromCheckpointsAndDetectsChangedInputs) {
    const FileSystemNs::path directory = FileSystemNs::temp_directory_path() / "kikirpa_test_pipeline_resume";
    const FileSystemNs::path checkpointDirectory = directory / "checkpoints";
    createTiles(directory);

    PipelineNs::PipelineClass pipeline = makeCheckpointedPipeline(directory, checkpointDirectory);
    std::string log;
    const auto logger = [&](const std::string& message) { log += message + "\n"; };
    ASSERT_TRUE(pipeline.run(logger));
    verifyCheckpointFiles(checkpointDirectory);
    const auto& matches = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    const float expectedTranslationX = matches.at(FIRST_MATCH_INDEX).featureMatches.translationX;

    log.clear();
    ASSERT_TRUE(pipeline.run(logger));
    EXPECT_NE(log.find("nothing to do"), std::string::npos);
    verifyRestoredPipelineOutputs(pipeline, expectedTranslationX);

    verifySourceChangeRebuildsPipeline(pipeline, directory);
    FileSystemNs::remove_all(directory);
}

} // namespace StitchingNs::TestsNs
