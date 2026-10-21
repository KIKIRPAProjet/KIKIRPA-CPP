#include "steps/LightGlueStep.h"
#include "steps/ImportStep.h"
#include "core/PipelineBuilder.h"
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace stitching;
using namespace stitching::steps;

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

enum class Mode { Valid, False, Empty, Mismatch, Nan, Outside, LowScore, Throw, SecondPairFails };
struct State {
    Mode mode = Mode::Valid;
    int creations = 0, calls = 0;
    std::vector<Direction> directions;
};

class TestMatcher : public matching::FeatureMatcher {
    std::shared_ptr<State> state;
public:
    explicit TestMatcher(std::shared_ptr<State> s) : state(std::move(s)) {}
    bool match(const cv::Mat&, const cv::Mat&, const PairContext& ctx, Matches& out) override {
        ++state->calls;
        state->directions.push_back(ctx.dir);
        if (state->mode == Mode::Throw) throw std::runtime_error("inference failure");
        if (state->mode == Mode::False || (state->mode == Mode::SecondPairFails && state->calls == 2)) return false;
        if (state->mode == Mode::Empty) return true;
        for (int i = 0; i < 4; ++i) {
            out.pointsA.emplace_back(6.f + i, 6.f);
            out.pointsB.emplace_back(4.f + i, 4.f);
            out.scores.push_back(0.98f);
        }
        out.dx = out.dy = 2.f;
        if (state->mode == Mode::Mismatch) out.scores.pop_back();
        if (state->mode == Mode::Nan) out.pointsA[0].x = std::numeric_limits<float>::quiet_NaN();
        if (state->mode == Mode::Outside) out.pointsB[0].x = 32.f;
        if (state->mode == Mode::LowScore) out.scores[0] = 0.1f;
        return true;
    }
};

LightGlueStep::MatcherFactory factory(const std::shared_ptr<State>& state) {
    return [state](const Json& params) {
        require(params.at("model_path").is_string(), "factory receives model_path");
        ++state->creations;
        return std::make_unique<TestMatcher>(state);
    };
}

Tiles pairTiles() {
    return {{"a.tif", 0, 0, cv::Mat(32, 32, CV_16UC1, cv::Scalar(2000))},
            {"b.tif", 1, 0, cv::Mat(32, 32, CV_16UC1, cv::Scalar(2000))}};
}

void gridAndReuse() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore store;
    Tiles tiles = pairTiles();
    tiles.push_back({"c.tif", 0, 1, tiles[0].image});
    tiles.push_back({"d.tif", 1, 1, tiles[0].image});
    std::swap(tiles[0], tiles[3]); // indices d'origine conservés malgré l'ordre de parcours de la grille
    store.set("tiles", tiles);
    step.setDataStore(store);
    require(step.execute(), "2x2 grid should succeed");
    const auto& results = store.get<PairMatches>("matches");
    require(results.size() == 4 && state->calls == 4, "each right/bottom pair exactly once");
    require(results[0].a.index == 3 && results[0].b.index == 1, "original tile indices");
    require(results[0].a.path == "a.tif", "tile paths are preserved");
    require(results[0].direction == Direction::Right && results[1].direction == Direction::Bottom,
            "horizontal and vertical contexts");
    require(results[0].matches.pointsA.size() == 4, "correspondences published");
    require(step.execute() && state->creations == 1, "session reused across executions");
    Json cfg = step.getConfig();
    cfg["options"][0]["value"] = "another.onnx";
    step.importConfig(cfg);
    require(step.execute() && state->creations == 2, "model change recreates matcher");
}

void failuresAndRetry() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore store;
    store.set("tiles", pairTiles());
    step.setDataStore(store);
    for (Mode mode : {Mode::False, Mode::Empty, Mode::Mismatch, Mode::Nan, Mode::Outside, Mode::LowScore, Mode::Throw}) {
        state->mode = Mode::Valid;
        require(step.execute(), "success before failure");
        state->mode = mode;
        require(!step.execute(), "invalid matcher result must fail");
        require(!store.has("matches") && step.getOutput().empty(), "stale output removed");
        require(step.exportData().contains("error") && step.exportData().contains("pair"), "pair diagnostic");
        state->mode = Mode::Valid;
        require(step.execute() && !step.exportData().contains("error"), "clean retry");
    }
    state->mode = Mode::SecondPairFails;
    state->calls = 0;
    auto tiles = pairTiles();
    tiles.push_back({"c.tif", 2, 0, tiles[0].image});
    store.set("tiles", tiles);
    require(!step.execute() && !store.has("matches") && step.getOutput().empty(), "no partial publication");
}

void invalidInputs() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    require(!step.execute(), "missing DataStore");
    DataStore store;
    step.setDataStore(store);
    require(!step.execute(), "missing tiles");
    store.set("tiles", 12);
    require(!step.execute(), "wrong Tiles type");
    store.set("tiles", Tiles{});
    require(!step.execute(), "empty grid");
    auto tiles = pairTiles();
    tiles[1].col = 0;
    store.set("tiles", tiles);
    require(!step.execute(), "duplicate coordinates");
    tiles[1].col = 2;
    store.set("tiles", tiles);
    require(!step.execute(), "non-adjacent grid");
    tiles = pairTiles();
    tiles[1].image = {};
    store.set("tiles", tiles);
    require(!step.execute(), "empty image");
    tiles[1].image = cv::Mat(32, 32, CV_32FC1);
    store.set("tiles", tiles);
    require(!step.execute(), "unsupported pixel depth");
    tiles = pairTiles();
    tiles[0].col = -1;
    store.set("tiles", tiles);
    require(!step.execute(), "negative coordinates");
    require(state->creations == 0, "invalid inputs do not load a model");
    tiles = pairTiles();
    tiles[0].col = std::numeric_limits<int>::max() - 1;
    tiles[1].col = std::numeric_limits<int>::max();
    store.set("tiles", tiles);
    require(step.execute(), "maximum grid coordinate without overflow");
}

void invalidConfiguration() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore store;
    store.set("tiles", pairTiles());
    step.setDataStore(store);
    const Json original = step.getConfig();
    for (const Json& value : {Json(0), Json(-1), Json(1.5), Json("4"), Json(4294967296ULL)}) {
        Json cfg = original;
        cfg["options"][1]["value"] = value;
        step.importConfig(cfg);
        require(!step.execute(), "invalid min_matches");
    }
    for (const Json& value : {Json(0), Json(1.1), Json("0.2")}) {
        Json cfg = original;
        cfg["options"][2]["value"] = value;
        step.importConfig(cfg);
        require(!step.execute(), "invalid overlap");
    }
    Json cfg = original;
    cfg["options"][0]["value"] = "";
    step.importConfig(cfg);
    require(!step.execute(), "empty model path");
    cfg = original;
    cfg["options"].push_back(cfg["options"][0]);
    step.importConfig(cfg);
    require(!step.execute(), "duplicate options");
    cfg = original;
    cfg["options"][1]["type"] = "string";
    step.importConfig(cfg);
    require(!step.execute(), "wrong declared option type");
    LightGlueStep nullFactory({"fixture.onnx"}, [](const Json&) {
        return std::unique_ptr<matching::FeatureMatcher>();
    });
    nullFactory.setDataStore(store);
    require(!nullFactory.execute(), "null matcher");
}

void importIntegration() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("lightglue-step-" + std::to_string(stamp));
    require(std::filesystem::create_directory(dir), "create isolated fixtures");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{dir};
    cv::Mat image(32, 32, CV_16UC1, cv::Scalar(2000));
    require(cv::imwrite((dir / "img_C00_R00.tif").string(), image), "write first fixture");
    require(cv::imwrite((dir / "img_C01_R00.tif").string(), image), "write second fixture");
    auto state = std::make_shared<State>();
    Pipeline pipeline;
    pipeline.add(std::make_unique<ImportStep>(ImportInput{dir.string()}));
    pipeline.add(std::make_unique<LightGlueStep>(LightGlueInput{"fixture.onnx"}, factory(state)));
    require(pipeline.run(), "ImportStep -> LightGlueStep");
    require(pipeline.getData().get<PairMatches>("matches").size() == 1, "integration output");
    Json cfg = pipeline.exportConfig();
    cfg["pipeline"]["steps"][0]["options"][0]["value"] = (dir / "missing").string();
    pipeline.configure(cfg);
    require(!pipeline.run() && !pipeline.getData().has("matches"), "import failure clears previous run results");
    PipelineSettings settings;
    settings.imageDir = dir;
    auto actual = buildLightGluePipeline(settings);
    require(!actual.run(), "real builder rejects missing model path");
}
} // namespace

int main() {
    int failures = 0;
    const std::pair<const char*, void (*)()> tests[] = {
        {"grid and session reuse", gridAndReuse}, {"failures and retry", failuresAndRetry},
        {"invalid inputs", invalidInputs}, {"configuration", invalidConfiguration},
        {"import integration", importIntegration}
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
