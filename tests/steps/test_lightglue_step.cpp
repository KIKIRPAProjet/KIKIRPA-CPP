// tests/steps/test_lightglue_step.cpp
#include <gtest/gtest.h>
#include "steps/lightglue_step.h"
#include "steps/matching_step_utils.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/data/tile.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace DataNs;
using namespace StepsNs;
using Json = nlohmann::json;
namespace matching = stitching::matching;
namespace {
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
        cv::Mat noise(560, 740, CV_32F);
        cv::RNG rng(12345);
        rng.fill(noise, cv::RNG::UNIFORM, 0.0, 1.0);
        cv::GaussianBlur(noise, noise, cv::Size(), 2.0);
        cv::normalize(noise, noise, 2000.0, 60000.0, cv::NORM_MINMAX);
        noise.convertTo(source, CV_16U);
    }
    ~Fixtures() { std::error_code ec; std::filesystem::remove_all(directory, ec); }
    std::shared_ptr<TileClass> tile(int id, int x, int y, int depth = CV_16U) {
        cv::Mat image;
        source(cv::Rect(x, y, 400, 300)).convertTo(image, depth, depth == CV_8U ? 1.0 / 257.0 : 1.0);
        const auto file = directory / (std::to_string(id) + ".tif");
        require(cv::imwrite(file.string(), image), "write tile");
        return std::make_shared<TileClass>(id, file, "", image.cols, image.rows, image.type());
    }
    TileList grid(int depth = CV_16U) {
        TileList tiles{tile(0, 0, 0, depth), tile(1, 320, 0, depth), tile(2, 0, 240, depth), tile(3, 320, 240, depth)};
        tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, tiles[2].get());
        tiles[1]->setNeighbours(tiles[0].get(), nullptr, nullptr, tiles[3].get());
        tiles[2]->setNeighbours(nullptr, tiles[3].get(), tiles[0].get(), nullptr);
        tiles[3]->setNeighbours(tiles[2].get(), nullptr, tiles[1].get(), nullptr);
        return tiles;
    }
};
enum class Mode { Valid, Fail, SecondFails, Throw, InvalidScore, Nan, Outside, Mismatch };
struct State {
    int calls = 0, creations = 0;
    Mode mode = Mode::Valid;
    float score = 0.1f;
};
class TestMatcher : public matching::FeatureMatcher {
    std::shared_ptr<State> state;
public:
    explicit TestMatcher(std::shared_ptr<State> s) : state(std::move(s)) {}
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& context, Matches& out) override {
        ++state->calls;
        // Vérifie que l'estimation réelle découpe les deux axes et transmet des bandes de même taille.
        require(a.size() == b.size(), "equal common regions");
        require(context.direction == Direction::Right ? a.cols == 80 && a.rows == 300 : a.cols == 400 && a.rows == 60,
                "matcher must receive overlap bands, never full tiles");
        if (state->mode == Mode::Throw) throw std::runtime_error("inference failure");
        if (state->mode == Mode::Fail || (state->mode == Mode::SecondFails && state->calls == 2)) return false;
        for (int i = 0; i < 4; ++i) {
            out.pointsInA.emplace_back(12.f + i, 12.f);
            out.pointsInB.emplace_back(10.f + i, 10.f);
            out.confidenceScores.push_back(state->score);
        }
        out.translationX = out.translationY = 2.f;
        if (state->mode == Mode::InvalidScore) out.confidenceScores[0] = 1.1f;
        if (state->mode == Mode::Nan) out.confidenceScores[0] = std::numeric_limits<float>::quiet_NaN();
        if (state->mode == Mode::Outside) out.pointsInA[0].x = static_cast<float>(a.cols);
        if (state->mode == Mode::Mismatch) out.pointsInB.pop_back();
        return true;
    }
};
LightGlueStepClass::MatcherFactory factory(const std::shared_ptr<State>& state) {
    return [state](const Json& params) {
        require(params.at("model_path").is_string(), "adapter to matcher model_path");
        ++state->creations;
        return std::make_unique<TestMatcher>(state);
    };
}
void cropsAndCoordinates(Fixtures& fixtures) {
    for (int depth : {CV_8U, CV_16U}) {
        auto state = std::make_shared<State>();
        LightGlueStepClass step({"fixture.onnx"}, factory(state));
        StepNs::DataStoreClass data;
        data.replace("tiles", fixtures.grid(depth));
        step.setDataStore(data);
        require(step.execute(), step.getResult().dump());
        const auto& pairs = step.getOutput().value;
        require(pairs.size() == 4 && state->calls == 4, "four neighbor pairs");
        for (const auto& pair : pairs) {
            const auto& m = pair.featureMatches;
            const bool right = pair.direction == Direction::Right;
            require(m.pointsInA[0].x == (right ? 332.f : 12.f) && m.pointsInA[0].y == (right ? 12.f : 252.f),
                    "A points restored to tile coordinates");
            require(m.pointsInB[0].x == 10.f && m.pointsInB[0].y == 10.f, "B points restored");
            require(m.translationX == (right ? 322.f : 2.f) && m.translationY == (right ? 2.f : 242.f), "translation restored");
            require(m.confidenceScores[0] == 0.1f, "step accepts finite scores below matcher threshold");
        }
        const Json serialized = step.getOutput();
        require(serialized.at("key") == "matches" && serialized.at("type") == "PairMatches" &&
                serialized.at("value").size() == 4, "key/value/type serialization");
        require(serialized["value"][0]["matches"]["pointsInA"][0] == Json::array({332.f, 12.f}), "serialized full coordinates");
        require(step.getResult().at("pairs") == 4 && step.getResult().at("failures").empty() &&
                !step.getResult().contains("value") && step.getResult().dump().find("pointsInA") == std::string::npos,
                "log contains summary only");
        require(data.get<std::vector<PairMatches>>("matches").size() == 4, "typed output published");
        require(step.execute() && state->creations == 1, "reuse model session");
        step.importConfig({{"parameters", {{{"name", "modelPath"}, {"valueType", "string"}, {"value", "other.onnx"}}}}});
        require(step.execute() && state->creations == 2, "reload changed model");
    }
}
void pairFailures(Fixtures& fixtures) {
    auto state = std::make_shared<State>();
    LightGlueStepClass step({"fixture.onnx"}, factory(state));
    StepNs::DataStoreClass data;
    data.replace("tiles", fixtures.grid());
    step.setDataStore(data);
    state->mode = Mode::SecondFails;
    require(step.execute() && step.getOutput().value.size() == 3, "one failed pair does not discard successes");
    require(step.getResult()["failures"].size() == 1 && step.getResult()["failures"][0]["idA"] == 0 &&
            step.getResult()["failures"][0]["idB"] == 2, "failed pair identified");
    for (auto mode : {Mode::Fail, Mode::Throw, Mode::InvalidScore, Mode::Nan, Mode::Outside, Mode::Mismatch}) {
        state->mode = mode;
        require(!step.execute(), "all pairs failed");
        require(step.getResult()["pairs"] == 0 && step.getResult()["failures"].size() == 4 &&
                step.getResult().contains("error"), "failures summarized");
        require(data.get<std::vector<PairMatches>>("matches").empty() && step.getOutput().value.empty(), "no stale results");
    }
    state->mode = Mode::Valid;
    require(step.execute() && step.getResult()["failures"].empty() && !step.getResult().contains("error"), "clean retry");
    auto tiles = fixtures.grid();
    std::filesystem::remove(tiles[3]->getSource());
    data.replace("tiles", tiles);
    require(step.execute() && step.getOutput().value.size() == 2 && step.getResult()["failures"].size() == 2,
            "unreadable image fails only its pairs");
    cv::Mat blank(300, 400, CV_16U, cv::Scalar(2000));
    require(cv::imwrite(tiles[0]->getSource().string(), blank) &&
            cv::imwrite(tiles[1]->getSource().string(), blank), "write unmeasurable tiles");
    data.replace("tiles", TileList{tiles[0], tiles[1]});
    tiles[0]->setNeighbours(nullptr, tiles[1].get(), nullptr, nullptr);
    tiles[1]->setNeighbours(tiles[0].get(), nullptr, nullptr, nullptr);
    const int calls = state->calls;
    require(!step.execute() && state->calls == calls && step.getResult()["failures"].size() == 1,
            "failed overlap estimation never invokes matcher");
}
void sharedCoordinateConversion() {
    Matches matches;
    matches.pointsInA = {{12.f, 13.f}};
    matches.pointsInB = {{10.f, 11.f}};
    matches.confidenceScores = {0.f};
    matches.translationX = matches.translationY = 2.f;
    validateMatches(matches, {50, 60}, {50, 60}, 1);
    restoreCoordinates(matches, {320, 0, 50, 60}, {0, 7, 50, 60});
    require(matches.pointsInA[0].x == 332.f && matches.pointsInB[0].y == 18.f &&
            matches.translationX == 322.f && matches.translationY == -5.f,
            "different origins in A and B, including negative transverse translation");
    validateMatches(matches, {400, 300}, {400, 300}, 1);
    const auto json = serializeOutput("matches", {{0, 1, Direction::Right, matches}}, "PairMatches");
    require(json["value"][0]["matches"]["pointsInB"][0] == Json::array({10.f, 18.f}), "serialize B origin");
}
void configurationAndRegistry(Fixtures& fixtures) {
    auto step = PipelineNs::createStep("lightglue");
    require(dynamic_cast<LightGlueStepClass*>(step.get()), "registered factory");
    Json script = {{"pipeline", {{"steps", Json::array({Json{{"name", "import"}}, Json{{"name", "lightglue"},
        {"parameters", {{{"valueType", "string"}, {"name", "modelPath"}, {"value", "fixture.onnx"}}}}}})}}}};
    const auto pipeline = PipelineNs::buildPipeline(script);
    require(pipeline.getSteps().size() == 2 && pipeline.getSteps()[1]->getConfigJson()["parameters"].size() == 3,
            "JSON builds import -> lightglue and preserves defaults");
    const auto reject = [&](Json cfg) {
        bool rejected = false;
        try { step->importConfig(cfg); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid import rejected by Step");
    };
    reject({{"parameters", {{{"valueType", "string"}, {"name", "model_path"}, {"value", "old.onnx"}}}}});
    reject({{"parameters", {{{"valueType", "int"}, {"name", "minMatches"}, {"value", 2.5}}}}});
    reject({{"parameters", {{{"valueType", "float"}, {"name", "overlap"}, {"value", 0.2}},
                          {{"valueType", "float"}, {"name", "overlap"}, {"value", 0.3}}}}});
    auto state = std::make_shared<State>();
    LightGlueStepClass tested({"fixture.onnx"}, factory(state));
    StepNs::DataStoreClass data;
    data.replace("tiles", fixtures.grid());
    tested.setDataStore(data);
    for (double overlap : {0.0, 1.0, -0.1}) {
        tested.importConfig({{"parameters", {{{"valueType", "float"}, {"name", "overlap"}, {"value", overlap}}}}});
        require(!tested.execute() && tested.getResult().contains("error"), "domain validation");
    }
    LightGlueStepClass invalid({"fixture.onnx", 0}, factory(state));
    invalid.setDataStore(data);
    require(!invalid.execute() && state->creations == 0, "constructor domain validation before loading model");
    data.replace("tiles", TileList{nullptr, nullptr});
    LightGlueStepClass malformed({"fixture.onnx"}, factory(state));
    malformed.setDataStore(data);
    require(!malformed.execute(), "invalid tile graph");
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
    data.replace("matches", std::vector<PairMatches>{{0, 1, Direction::Right, {}}});
    LightGlueStepClass step({"/missing/lightglue-model.onnx"});
    step.setDataStore(data);
    EXPECT_FALSE(step.execute());
    EXPECT_TRUE(step.getResult().contains("error"));
    EXPECT_TRUE(step.getOutput().value.empty());
    EXPECT_TRUE(data.get<std::vector<PairMatches>>("matches").empty());
}
