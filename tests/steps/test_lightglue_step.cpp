// tests/steps/test_lightglue_step.cpp
#include <gtest/gtest.h>
#include "algorithms/stitching/feature_matcher.h"
#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"
#include "steps/lightglue_step.h"
#include "steps/matching_step_utils.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/data/tile.h"
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <memory>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using Json = nlohmann::json;
namespace {
constexpr float LOCAL_TRANSLATION = 2.f;
constexpr double TEXTURE_SIGMA = 2.0;
constexpr int TEXTURE_HEIGHT = 560;
constexpr int TEXTURE_WIDTH = 740;
constexpr int TEXTURE_SEED = 12345;
constexpr double PIXEL_MINIMUM = 2000.0;
constexpr double PIXEL_MAXIMUM = 60000.0;
constexpr double DEPTH_SCALE = 257.0;
constexpr int TILE_WIDTH = 400;
constexpr int TILE_HEIGHT = 300;
constexpr int HORIZONTAL_SHIFT = 320;
constexpr int VERTICAL_SHIFT = 240;
constexpr int OVERLAP_WIDTH = 80;
constexpr int REGION_HEIGHT = 60;
constexpr int REGION_WIDTH = 50;
constexpr int TRANSVERSE_SHIFT = 7;
constexpr int BLANK_PIXEL = 2000;
constexpr float POINT_A = 12.f;
constexpr float POINT_B = 10.f;
constexpr float LOW_CONFIDENCE = 0.1f;
constexpr float INVALID_CONFIDENCE = 1.1f;
constexpr float RESTORED_POINT_A_X = 332.f;
constexpr float RESTORED_POINT_A_Y = 252.f;
constexpr float RESTORED_TRANSLATION_X = 322.f;
constexpr float RESTORED_TRANSLATION_Y = 242.f;
constexpr float POINT_A_Y = 13.f;
constexpr float POINT_B_Y = 11.f;
constexpr float RESTORED_POINT_B_Y = 18.f;
constexpr float TRANSVERSE_RESIDUAL = 5.f;
constexpr double FRACTIONAL_COUNT = 2.5;
constexpr double NOMINAL_OVERLAP = 0.2;
constexpr double ALTERNATE_OVERLAP = 0.3;
constexpr double INVALID_OVERLAP_MAGNITUDE = 0.1;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fixtures {
    std::filesystem::path directory;
    cv::Mat source;
    Fixtures() {
        directory = std::filesystem::temp_directory_path() /
            ("lightglue-review-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(directory), "create fixtures");
        cv::Mat noise(TEXTURE_HEIGHT, TEXTURE_WIDTH, CV_32F);
        cv::RNG rng(TEXTURE_SEED);
        rng.fill(noise, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(noise, noise, cv::Size(), TEXTURE_SIGMA);
        cv::normalize(noise, noise, PIXEL_MINIMUM, PIXEL_MAXIMUM, cv::NORM_MINMAX);
        noise.convertTo(source, CV_16U);
    }
    ~Fixtures() { std::error_code ec; std::filesystem::remove_all(directory, ec); }
    std::shared_ptr<DataNs::TileClass> tile(int id, cv::Point origin, int depth = CV_16U) {
        cv::Mat image;
        source(cv::Rect(origin.x, origin.y, TILE_WIDTH, TILE_HEIGHT)).convertTo(image, depth, depth == CV_8U ? 1.0 / DEPTH_SCALE : 1.0);
        const auto file = directory / (std::to_string(id) + ".tif");
        require(cv::imwrite(file.string(), image), "write tile");
        return std::make_shared<DataNs::TileClass>(id, file, "", image.cols, image.rows, image.type());
    }
    DataNs::TileList grid(int depth = CV_16U) {
        DataNs::TileList tiles{tile(0, {0, 0}, depth), tile(1, {HORIZONTAL_SHIFT, 0}, depth), tile(2, {0, VERTICAL_SHIFT}, depth), tile(3, {HORIZONTAL_SHIFT, VERTICAL_SHIFT}, depth)};
        tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, tiles[2].get());
        tiles[1]->setNeighbours(tiles[0].get(), nullptr, nullptr, tiles[3].get());
        tiles[2]->setNeighbours(nullptr, tiles[3].get(), tiles[0].get(), nullptr);
        tiles[3]->setNeighbours(tiles[2].get(), nullptr, tiles[1].get(), nullptr);
        return tiles;
    }
};
enum class Mode : unsigned char { Valid, Fail, SecondFails, Throw, InvalidScore, Nan, Outside, Mismatch };
struct State {
    int calls = 0, creations = 0;
    Mode mode = Mode::Valid;
    float score = LOW_CONFIDENCE;
};
class TestMatcherClass : public StitchingNs::FeatureMatcherClass {
    std::shared_ptr<State> state;
public:
    explicit TestMatcherClass(std::shared_ptr<State> s) : state(std::move(s)) {}
    bool match(const cv::Mat& a, const cv::Mat& b, const DataNs::PairContext& context, DataNs::Matches& out) override {
        ++state->calls;
        // Vérifie que l'estimation réelle découpe les deux axes et transmet des bandes de même taille.
        require(a.size() == b.size(), "equal common regions");
        require(context.direction == DataNs::Direction::Right ? a.cols == OVERLAP_WIDTH && a.rows == TILE_HEIGHT : a.cols == TILE_WIDTH && a.rows == REGION_HEIGHT,
                "matcher must receive overlap bands, never full tiles");
        if (state->mode == Mode::Throw) throw std::runtime_error("inference failure");
        if (state->mode == Mode::Fail || (state->mode == Mode::SecondFails && state->calls == 2)) return false;
        for (int i = 0; i < 4; ++i) {
            out.pointsInA.emplace_back(POINT_A + static_cast<float>(i), POINT_A);
            out.pointsInB.emplace_back(POINT_B + static_cast<float>(i), POINT_B);
            out.confidenceScores.push_back(state->score);
        }
        out.translationX = out.translationY = LOCAL_TRANSLATION;
        if (state->mode == Mode::InvalidScore) out.confidenceScores[0] = INVALID_CONFIDENCE;
        if (state->mode == Mode::Nan) out.confidenceScores[0] = std::numeric_limits<float>::quiet_NaN();
        if (state->mode == Mode::Outside) out.pointsInA[0].x = static_cast<float>(a.cols);
        if (state->mode == Mode::Mismatch) out.pointsInB.pop_back();
        return true;
    }
};
StepsNs::LightGlueStepClass::MatcherFactory factory(const std::shared_ptr<State>& state) {
    return [state](const Json& params) {
        require(params.at("model_path").is_string(), "adapter to matcher model_path");
        ++state->creations;
        return std::make_unique<TestMatcherClass>(state);
    };
}
void checkCoordinates(const std::vector<DataNs::PairMatches>& pairs) {
    for (const auto& pair : pairs) {
        const auto& m = pair.featureMatches;
        const bool right = pair.direction == DataNs::Direction::Right;
        require(m.pointsInA[0].x == (right ? RESTORED_POINT_A_X : POINT_A) && m.pointsInA[0].y == (right ? POINT_A : RESTORED_POINT_A_Y),
                "A points restored to tile coordinates");
        require(m.pointsInB[0].x == POINT_B && m.pointsInB[0].y == POINT_B, "B points restored");
        require(m.translationX == (right ? RESTORED_TRANSLATION_X : LOCAL_TRANSLATION) && m.translationY == (right ? LOCAL_TRANSLATION : RESTORED_TRANSLATION_Y), "translation restored");
        require(m.confidenceScores[0] == LOW_CONFIDENCE, "step accepts finite scores below matcher threshold");
    }
}
void checkOutput(const StepsNs::LightGlueStepClass& step, const StepNs::DataStoreClass& data) {
    const Json serialized = step.getOutput();
    require(serialized.at("key") == "matches" && serialized.at("type") == "PairMatches" &&
            serialized.at("value").size() == 4, "key/value/type serialization");
    require(serialized["value"][0]["matches"]["pointsInA"][0] == Json::array({RESTORED_POINT_A_X, POINT_A}), "serialized full coordinates");
    require(step.getResult().at("pairs") == 4 && step.getResult().at("failures").empty() &&
            !step.getResult().contains("value") && step.getResult().dump().find("pointsInA") == std::string::npos,
            "log contains summary only");
    require(data.get<std::vector<DataNs::PairMatches>>("matches").size() == 4, "typed output published");
}
void cropsAndCoordinates(Fixtures& fixtures) {
    for (int depth : {CV_8U, CV_16U}) {
        auto state = std::make_shared<State>();
        StepsNs::LightGlueStepClass step({"fixture.onnx"}, factory(state));
        StepNs::DataStoreClass data;
        data.replace("tiles", fixtures.grid(depth));
        step.setDataStore(data);
        require(step.execute(), step.getResult().dump());
        const auto& pairs = step.getOutput().value;
        require(pairs.size() == 4 && state->calls == 4, "four neighbor pairs");
        checkCoordinates(pairs);
        checkOutput(step, data);
        require(step.execute() && state->creations == 1, "reuse model session");
        step.importConfig({{"parameters", {{{"name", "modelPath"}, {"valueType", "string"}, {"value", "other.onnx"}}}}});
        require(step.execute() && state->creations == 2, "reload changed model");
    }
}
void checkMatcherFailures(StepsNs::LightGlueStepClass& step, const std::shared_ptr<State>& state,
                          const StepNs::DataStoreClass& data) {
    state->mode = Mode::SecondFails;
    require(step.execute() && step.getOutput().value.size() == 3, "one failed pair does not discard successes");
    require(step.getResult()["failures"].size() == 1 && step.getResult()["failures"][0]["idA"] == 0 &&
            step.getResult()["failures"][0]["idB"] == 2, "failed pair identified");
    for (auto mode : {Mode::Fail, Mode::Throw, Mode::InvalidScore, Mode::Nan, Mode::Outside, Mode::Mismatch}) {
        state->mode = mode;
        require(!step.execute(), "all pairs failed");
        require(step.getResult()["pairs"] == 0 && step.getResult()["failures"].size() == 4 &&
                step.getResult().contains("error"), "failures summarized");
        require(data.get<std::vector<DataNs::PairMatches>>("matches").empty() && step.getOutput().value.empty(), "no stale results");
    }
    state->mode = Mode::Valid;
    require(step.execute() && step.getResult()["failures"].empty() && !step.getResult().contains("error"), "clean retry");
}
void pairFailures(Fixtures& fixtures) {
    auto state = std::make_shared<State>();
    StepsNs::LightGlueStepClass step({"fixture.onnx"}, factory(state));
    StepNs::DataStoreClass data;
    data.replace("tiles", fixtures.grid());
    step.setDataStore(data);
    checkMatcherFailures(step, state, data);
    auto tiles = fixtures.grid();
    std::filesystem::remove(tiles[3]->getSource());
    data.replace("tiles", tiles);
    require(step.execute() && step.getOutput().value.size() == 2 && step.getResult()["failures"].size() == 2,
            "unreadable image fails only its pairs");
    cv::Mat blank(TILE_HEIGHT, TILE_WIDTH, CV_16U, cv::Scalar(BLANK_PIXEL));
    require(cv::imwrite(tiles[0]->getSource().string(), blank) &&
            cv::imwrite(tiles[1]->getSource().string(), blank), "write unmeasurable tiles");
    data.replace("tiles", DataNs::TileList{tiles[0], tiles[1]});
    tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, nullptr);
    tiles[1]->setNeighbours(tiles[0].get(), nullptr, nullptr, nullptr);
    const int calls = state->calls;
    require(!step.execute() && state->calls == calls && step.getResult()["failures"].size() == 1,
            "failed overlap estimation never invokes matcher");
}
void sharedCoordinateConversion() {
    DataNs::Matches matches;
    matches.pointsInA = {{POINT_A, POINT_A_Y}};
    matches.pointsInB = {{POINT_B, POINT_B_Y}};
    matches.confidenceScores = {0.f};
    matches.translationX = matches.translationY = LOCAL_TRANSLATION;
    StepsNs::validateMatches(matches, {REGION_WIDTH, REGION_HEIGHT}, {REGION_WIDTH, REGION_HEIGHT}, 1);
    StepsNs::restoreCoordinates(matches, {HORIZONTAL_SHIFT, 0, REGION_WIDTH, REGION_HEIGHT}, {0, TRANSVERSE_SHIFT, REGION_WIDTH, REGION_HEIGHT});
    require(matches.pointsInA[0].x == RESTORED_POINT_A_X && matches.pointsInB[0].y == RESTORED_POINT_B_Y &&
            matches.translationX == RESTORED_TRANSLATION_X && matches.translationY == -TRANSVERSE_RESIDUAL,
            "different origins in A and B, including negative transverse translation");
    StepsNs::validateMatches(matches, {TILE_WIDTH, TILE_HEIGHT}, {TILE_WIDTH, TILE_HEIGHT}, 1);
    const auto json = StepsNs::serializeOutput("matches", {{0, 1, DataNs::Direction::Right, matches}}, "PairMatches");
    require(json["value"][0]["matches"]["pointsInB"][0] == Json::array({POINT_B, RESTORED_POINT_B_Y}), "serialize B origin");
}
void checkInvalidDomain(Fixtures& fixtures) {
    auto state = std::make_shared<State>();
    StepsNs::LightGlueStepClass tested({"fixture.onnx"}, factory(state));
    StepNs::DataStoreClass data;
    data.replace("tiles", fixtures.grid());
    tested.setDataStore(data);
    for (double overlap : {0.0, 1.0, -INVALID_OVERLAP_MAGNITUDE}) {
        tested.importConfig({{"parameters", {{{"valueType", "float"}, {"name", "overlap"}, {"value", overlap}}}}});
        require(!tested.execute() && tested.getResult().contains("error"), "domain validation");
    }
    StepsNs::LightGlueStepClass invalid({"fixture.onnx", 0}, factory(state));
    invalid.setDataStore(data);
    require(!invalid.execute() && state->creations == 0, "constructor domain validation before loading model");
    data.replace("tiles", DataNs::TileList{nullptr, nullptr});
    StepsNs::LightGlueStepClass malformed({"fixture.onnx"}, factory(state));
    malformed.setDataStore(data);
    require(!malformed.execute(), "invalid tile graph");
}
void configurationAndRegistry(Fixtures& fixtures) {
    auto step = PipelineNs::createStep("lightglue");
    require(dynamic_cast<StepsNs::LightGlueStepClass*>(step.get()), "registered factory");
    Json script = {{"pipeline", {{"steps", Json::array({Json{{"name", "import"}}, Json{{"name", "lightglue"},
        {"parameters", {{{"valueType", "string"}, {"name", "modelPath"}, {"value", "fixture.onnx"}}}}}})}}}};
    const auto pipeline = PipelineNs::buildPipeline(script);
    require(pipeline.getSteps().size() == 2 && pipeline.getSteps()[1]->getConfigJson()["parameters"].size() == 3,
            "JSON builds import -> lightglue and preserves defaults");
    const auto reject = [&](const Json& cfg) {
        bool rejected = false;
        try { step->importConfig(cfg); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid import rejected by Step");
    };
    reject({{"parameters", {{{"valueType", "string"}, {"name", "model_path"}, {"value", "old.onnx"}}}}});
    reject({{"parameters", {{{"valueType", "int"}, {"name", "minMatches"}, {"value", FRACTIONAL_COUNT}}}}});
    reject({{"parameters", {{{"valueType", "float"}, {"name", "overlap"}, {"value", NOMINAL_OVERLAP}},
                          {{"valueType", "float"}, {"name", "overlap"}, {"value", ALTERNATE_OVERLAP}}}}});
    checkInvalidDomain(fixtures);
}
}
TEST(LightGlueStepTest, SharedCoordinates) { sharedCoordinateConversion(); }
TEST(LightGlueStepTest, CropsAndCoordinates) { Fixtures fixtures; cropsAndCoordinates(fixtures); }
TEST(LightGlueStepTest, PairFailuresAndRetry) { Fixtures fixtures; pairFailures(fixtures); }
TEST(LightGlueStepTest, ConfigurationAndRegistry) { Fixtures fixtures; configurationAndRegistry(fixtures); }

TEST(LightGlueStepTest, ModelLoadFailureClearsPreviousResults) {
    Fixtures fixtures;
    StepNs::DataStoreClass data;
    data.replace("tiles", fixtures.grid());
    data.replace("matches", std::vector<DataNs::PairMatches>{{0, 1, DataNs::Direction::Right, {}}});
    StepsNs::LightGlueStepClass step({"/missing/lightglue-model.onnx"});
    step.setDataStore(data);
    EXPECT_FALSE(step.execute());
    EXPECT_TRUE(step.getResult().contains("error"));
    EXPECT_TRUE(step.getOutput().value.empty());
    EXPECT_TRUE(data.get<std::vector<DataNs::PairMatches>>("matches").empty());
}
