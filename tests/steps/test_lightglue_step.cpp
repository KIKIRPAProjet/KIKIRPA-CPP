#include "steps/LightGlueStep.h"
#include "steps/MatchingStepUtils.h"
#include "core/PipelineBuilder.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace stitching;
using namespace stitching::steps;
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
    std::shared_ptr<Tuile> tile(int id, int x, int y, int depth = CV_16U) {
        cv::Mat image;
        source(cv::Rect(x, y, 400, 300)).convertTo(image, depth, depth == CV_8U ? 1.0 / 257.0 : 1.0);
        const auto file = directory / (std::to_string(id) + ".tif");
        require(cv::imwrite(file.string(), image), "write tile");
        return std::make_shared<Tuile>(id, file, "", image.cols, image.rows, image.type());
    }
    Tiles grid(int depth = CV_16U) {
        Tiles tiles{tile(0, 0, 0, depth), tile(1, 320, 0, depth), tile(2, 0, 240, depth), tile(3, 320, 240, depth)};
        tiles[0]->setVoisins(nullptr, tiles[1].get(), nullptr, tiles[2].get());
        tiles[1]->setVoisins(tiles[0].get(), nullptr, nullptr, tiles[3].get());
        tiles[2]->setVoisins(nullptr, tiles[3].get(), tiles[0].get(), nullptr);
        tiles[3]->setVoisins(tiles[2].get(), nullptr, tiles[1].get(), nullptr);
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
        require(context.dir == Direction::Right ? a.cols == 80 && a.rows == 300 : a.cols == 400 && a.rows == 60,
                "matcher must receive overlap bands, never full tiles");
        if (state->mode == Mode::Throw) throw std::runtime_error("inference failure");
        if (state->mode == Mode::Fail || (state->mode == Mode::SecondFails && state->calls == 2)) return false;
        for (int i = 0; i < 4; ++i) {
            out.pointsA.emplace_back(12.f + i, 12.f);
            out.pointsB.emplace_back(10.f + i, 10.f);
            out.scores.push_back(state->score);
        }
        out.dx = out.dy = 2.f;
        if (state->mode == Mode::InvalidScore) out.scores[0] = 1.1f;
        if (state->mode == Mode::Nan) out.scores[0] = std::numeric_limits<float>::quiet_NaN();
        if (state->mode == Mode::Outside) out.pointsA[0].x = static_cast<float>(a.cols);
        if (state->mode == Mode::Mismatch) out.pointsB.pop_back();
        return true;
    }
};
LightGlueStep::MatcherFactory factory(const std::shared_ptr<State>& state) {
    return [state](const Json& params) {
        require(params.at("model_path").is_string(), "adapter to matcher model_path");
        ++state->creations;
        return std::make_unique<TestMatcher>(state);
    };
}
void cropsAndCoordinates(Fixtures& fixtures) {
    for (int depth : {CV_8U, CV_16U}) {
        auto state = std::make_shared<State>();
        LightGlueStep step({"fixture.onnx"}, factory(state));
        DataStore data;
        data.set("tiles", fixtures.grid(depth));
        step.setDataStore(data);
        require(step.execute(), step.exportData().dump());
        const auto& pairs = step.getOutput().value;
        require(pairs.size() == 4 && state->calls == 4, "four neighbor pairs");
        for (const auto& pair : pairs) {
            const auto& m = pair.matches;
            const bool right = pair.dir == Direction::Right;
            require(m.pointsA[0].x == (right ? 332.f : 12.f) && m.pointsA[0].y == (right ? 12.f : 252.f),
                    "A points restored to tile coordinates");
            require(m.pointsB[0].x == 10.f && m.pointsB[0].y == 10.f, "B points restored");
            require(m.dx == (right ? 322.f : 2.f) && m.dy == (right ? 2.f : 242.f), "translation restored");
            require(m.scores[0] == 0.1f, "step accepts finite scores below matcher threshold");
        }
        const Json serialized = step.getOutput();
        require(serialized.at("key") == "matches" && serialized.at("type") == "PairMatches" &&
                serialized.at("value").size() == 4, "key/value/type serialization");
        require(serialized["value"][0]["matches"]["pointsA"][0] == Json::array({332.f, 12.f}), "serialized full coordinates");
        require(step.exportData().at("pairs") == 4 && step.exportData().at("failures").empty() &&
                !step.exportData().contains("value") && step.exportData().dump().find("pointsA") == std::string::npos,
                "log contains summary only");
        require(data.get<std::vector<PairMatches>>("matches").size() == 4, "typed output published");
        require(step.execute() && state->creations == 1, "reuse model session");
        step.importConfig({{"options", {{{"key", "modelPath"}, {"type", "string"}, {"value", "other.onnx"}}}}});
        require(step.execute() && state->creations == 2, "reload changed model");
    }
}
void pairFailures(Fixtures& fixtures) {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore data;
    data.set("tiles", fixtures.grid());
    step.setDataStore(data);
    state->mode = Mode::SecondFails;
    require(step.execute() && step.getOutput().value.size() == 3, "one failed pair does not discard successes");
    require(step.exportData()["failures"].size() == 1 && step.exportData()["failures"][0]["idA"] == 0 &&
            step.exportData()["failures"][0]["idB"] == 2, "failed pair identified");
    for (auto mode : {Mode::Fail, Mode::Throw, Mode::InvalidScore, Mode::Nan, Mode::Outside, Mode::Mismatch}) {
        state->mode = mode;
        require(!step.execute(), "all pairs failed");
        require(step.exportData()["pairs"] == 0 && step.exportData()["failures"].size() == 4 &&
                step.exportData().contains("error"), "failures summarized");
        require(!data.has("matches") && step.getOutput().value.empty(), "no stale results");
    }
    state->mode = Mode::Valid;
    require(step.execute() && step.exportData()["failures"].empty() && !step.exportData().contains("error"), "clean retry");
    auto tiles = fixtures.grid();
    std::filesystem::remove(tiles[3]->getSource());
    data.set("tiles", tiles);
    require(step.execute() && step.getOutput().value.size() == 2 && step.exportData()["failures"].size() == 2,
            "unreadable image fails only its pairs");
    cv::Mat blank(300, 400, CV_16U, cv::Scalar(2000));
    require(cv::imwrite(tiles[1]->getSource().string(), blank), "write unmeasurable tile");
    data.set("tiles", Tiles{tiles[0], tiles[1]});
    tiles[0]->setVoisins(nullptr, tiles[1].get(), nullptr, nullptr);
    const int calls = state->calls;
    require(!step.execute() && state->calls == calls && step.exportData()["failures"].size() == 1,
            "failed overlap estimation never invokes matcher");
}
void sharedCoordinateConversion() {
    Matches matches;
    matches.pointsA = {{12.f, 13.f}};
    matches.pointsB = {{10.f, 11.f}};
    matches.scores = {0.f};
    matches.dx = matches.dy = 2.f;
    matching_step::validateMatches(matches, {50, 60}, {50, 60}, 1);
    matching_step::restoreCoordinates(matches, {320, 0, 50, 60}, {0, 7, 50, 60});
    require(matches.pointsA[0].x == 332.f && matches.pointsB[0].y == 18.f &&
            matches.dx == 322.f && matches.dy == -5.f,
            "different origins in A and B, including negative transverse translation");
    matching_step::validateMatches(matches, {400, 300}, {400, 300}, 1);
    const auto json = matching_step::serializeOutput("matches", {{0, 1, Direction::Right, matches}}, "PairMatches");
    require(json["value"][0]["matches"]["pointsB"][0] == Json::array({10.f, 18.f}), "serialize B origin");
}
void configurationAndRegistry(Fixtures& fixtures) {
    auto step = createStep("lightglue");
    require(dynamic_cast<LightGlueStep*>(step.get()), "registered factory");
    Json script = {{"pipeline", {{"steps", Json::array({Json{{"name", "import"}}, Json{{"name", "lightglue"},
        {"options", {{{"type", "string"}, {"key", "modelPath"}, {"value", "fixture.onnx"}}}}}})}}}};
    const auto pipeline = buildPipeline(script);
    require(pipeline.getSteps().size() == 2 && pipeline.getSteps()[1]->getConfig()["options"].size() == 3,
            "JSON builds import -> lightglue and preserves defaults");
    const auto reject = [&](Json cfg) {
        bool rejected = false;
        try { step->importConfig(cfg); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid import rejected by Step");
    };
    reject({{"options", {{{"type", "string"}, {"key", "model_path"}, {"value", "old.onnx"}}}}});
    reject({{"options", {{{"type", "int"}, {"key", "minMatches"}, {"value", 2.5}}}}});
    reject({{"options", {{{"type", "float"}, {"key", "overlap"}, {"value", 0.2}},
                          {{"type", "float"}, {"key", "overlap"}, {"value", 0.3}}}}});
    auto state = std::make_shared<State>();
    LightGlueStep tested({"fixture.onnx"}, factory(state));
    DataStore data;
    data.set("tiles", fixtures.grid());
    tested.setDataStore(data);
    for (double overlap : {0.0, 1.0, -0.1}) {
        tested.importConfig({{"options", {{{"type", "float"}, {"key", "overlap"}, {"value", overlap}}}}});
        require(!tested.execute() && tested.exportData().contains("error"), "domain validation");
    }
    LightGlueStep invalid({"fixture.onnx", 0}, factory(state));
    invalid.setDataStore(data);
    require(!invalid.execute() && state->creations == 0, "constructor domain validation before loading model");
    data.set("tiles", Tiles{nullptr, nullptr});
    LightGlueStep malformed({"fixture.onnx"}, factory(state));
    malformed.setDataStore(data);
    require(!malformed.execute(), "invalid tile graph");
}
}
int main() {
    try {
        sharedCoordinateConversion();
        Fixtures fixtures;
        cropsAndCoordinates(fixtures);
        pairFailures(fixtures);
        configurationAndRegistry(fixtures);
        std::cout << "PASS LightGlue step\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
