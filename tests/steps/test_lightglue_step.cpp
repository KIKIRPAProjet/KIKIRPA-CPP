#include "steps/LightGlueStep.h"
#include "steps/ImportStep.h"
#include "core/Tuile.h"
#include <algorithm>
#include <set>
#include "LightGlueTestPipeline.h"
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

struct FixtureDirectory {
    std::filesystem::path path;
    FixtureDirectory() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("lightglue-fixtures-" + std::to_string(stamp));
        require(std::filesystem::create_directory(path), "create isolated fixtures");
    }
    ~FixtureDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::shared_ptr<Tuile> makeTile(int id, const cv::Mat& image = cv::Mat(32, 32, CV_16UC1, cv::Scalar(2000))) {
    static FixtureDirectory directory;
    const auto path = directory.path / (std::to_string(id) + ".tif");
    require(cv::imwrite(path.string(), image), "write tile fixture");
    return std::make_shared<Tuile>(id, path, std::filesystem::path{}, image.cols, image.rows, image.type());
}

Tiles pairTiles() {
    auto a = makeTile(0), b = makeTile(1);
    a->setVoisins(nullptr, b.get(), nullptr, nullptr);
    b->setVoisins(a.get(), nullptr, nullptr, nullptr);
    return {a, b};
}

void gridAndReuse() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore store;
    Tiles tiles = pairTiles();
    auto c = makeTile(2), d = makeTile(3);
    tiles[0]->setVoisins(nullptr, tiles[1].get(), nullptr, c.get());
    tiles[1]->setVoisins(tiles[0].get(), nullptr, nullptr, d.get());
    c->setVoisins(nullptr, d.get(), tiles[0].get(), nullptr);
    d->setVoisins(c.get(), nullptr, tiles[1].get(), nullptr);
    tiles.push_back(c);
    tiles.push_back(d);
    std::swap(tiles[0], tiles[3]);
    store.set("tiles", tiles);
    step.setDataStore(store);
    require(step.execute(), "2x2 grid should succeed");
    const auto& results = store.get<std::vector<PairMatches>>("matches");
    require(results.size() == 4 && state->calls == 4, "each right/bottom pair exactly once");
    std::set<std::pair<int, int>> edges;
    for (const auto& result : results) edges.emplace(result.idA, result.idB);
    require(edges == std::set<std::pair<int, int>>{{0, 1}, {0, 2}, {1, 3}, {2, 3}}, "tile ids and no duplicates");
    require(std::count(state->directions.begin(), state->directions.end(), Direction::Right) == 2 &&
            std::count(state->directions.begin(), state->directions.end(), Direction::Bottom) == 2,
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
        require(!store.has("matches") && step.getOutput().value.empty(), "stale output removed");
        require(step.exportData().contains("error") && step.exportData().contains("pair"), "pair diagnostic");
        state->mode = Mode::Valid;
        require(step.execute() && !step.exportData().contains("error"), "clean retry");
    }
    state->mode = Mode::SecondPairFails;
    state->calls = 0;
    auto tiles = pairTiles();
    auto c = makeTile(2);
    tiles[1]->setVoisins(tiles[0].get(), c.get(), nullptr, nullptr);
    tiles.push_back(c);
    store.set("tiles", tiles);
    require(!step.execute() && !store.has("matches") && step.getOutput().value.empty(), "no partial publication");
}

void outputSerialization() {
    auto state = std::make_shared<State>();
    LightGlueStep step({"fixture.onnx"}, factory(state));
    DataStore store;
    auto tiles = pairTiles();
    store.set("tiles", tiles);
    step.setDataStore(store);
    require(step.execute(), "serialization fixture");
    const auto& output = step.getOutput();
    const Json serialized = output;
    const Json decoded = Json::parse(serialized.dump());
    require(decoded.size() == 3 && decoded.at("key") == "matches" && decoded.at("type") == "PairMatches",
            "output envelope uses key, value and type");
    require(decoded.at("value").is_array() && decoded.at("value").size() == 1, "array payload");
    const auto& pair = decoded.at("value").at(0);
    require(pair.at("idA") == 0 && pair.at("idB") == 1, "tile ids serialized");
    require(pair.at("direction") == "right", "direction serialized by name");
    require(pair.at("matches").at("pointsA").at(0) == Json::array({6.f, 6.f}) &&
            pair.at("matches").at("pointsB").at(0) == Json::array({4.f, 4.f}), "point coordinates serialized");
    require(pair.at("matches").at("scores").size() == 4 && pair.at("matches").at("dx") == 2.f,
            "confidence and translation serialized");
    require(step.exportData() == serialized, "exportData exposes the output envelope");
    require(store.get<std::vector<PairMatches>>(output.key).size() == output.value.size(), "typed DataStore contract retained");
    require(step.getConfig().at("signature").at("pipelineOptions").at(0).at("type") == output.type,
            "signature and output type agree");
    state->mode = Mode::False;
    require(!step.execute() && step.exportData().contains("error") && step.getOutput().value.empty(),
            "failure clears serialized payload");
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
    tiles[1] = tiles[0];
    store.set("tiles", tiles);
    require(!step.execute(), "duplicate tile");
    tiles = pairTiles();
    tiles[1]->setVoisins(nullptr, nullptr, nullptr, nullptr);
    tiles[0]->setVoisins(nullptr, nullptr, nullptr, nullptr);
    store.set("tiles", tiles);
    require(!step.execute(), "non-adjacent tiles");
    tiles = pairTiles();
    tiles[1] = nullptr;
    store.set("tiles", tiles);
    require(!step.execute(), "null tile");
    tiles = pairTiles();
    auto floating = makeTile(1, cv::Mat(32, 32, CV_32FC1, cv::Scalar(1)));
    tiles[0]->setVoisins(nullptr, floating.get(), nullptr, nullptr);
    store.set("tiles", Tiles{tiles[0], floating});
    require(!step.execute(), "unsupported pixel depth");
    auto negative = makeTile(-1);
    store.set("tiles", Tiles{negative, tiles[0]});
    require(!step.execute(), "negative tile id");
    require(state->creations == 0, "invalid inputs do not load a model");
    auto largeA = makeTile(std::numeric_limits<int>::max() - 1);
    auto largeB = makeTile(std::numeric_limits<int>::max());
    largeA->setVoisins(nullptr, largeB.get(), nullptr, nullptr);
    store.set("tiles", Tiles{largeA, largeB});
    require(step.execute(), "large tile ids");
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
    auto pipeline = stitching::tests::buildLightGlueTestPipeline(
        ImportInput{dir.string()}, LightGlueInput{"fixture.onnx"}, factory(state));
    require(pipeline.run(), "ImportStep -> LightGlueStep");
    require(pipeline.getData().get<std::vector<PairMatches>>("matches").size() == 1, "integration output");
    Json cfg = pipeline.exportConfig();
    cfg["pipeline"]["steps"][0]["options"][0]["value"] = (dir / "missing").string();
    pipeline.configure(cfg);
    require(!pipeline.run() && !pipeline.getData().has("matches"), "import failure clears previous run results");
    auto actual = stitching::tests::buildLightGlueTestPipeline(ImportInput{dir.string()}, LightGlueInput{});
    require(!actual.run(), "test pipeline rejects missing model path");
}
} // namespace

int main() {
    int failures = 0;
    const std::pair<const char*, void (*)()> tests[] = {
        {"grid and session reuse", gridAndReuse}, {"failures and retry", failuresAndRetry},
        {"output serialization", outputSerialization},
        {"invalid inputs", invalidInputs}, {"configuration", invalidConfiguration},
        {"import integration", importIntegration}
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
