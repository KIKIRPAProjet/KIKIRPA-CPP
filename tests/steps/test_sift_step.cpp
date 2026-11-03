#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"
#include "steps/sift_step.h"
#include "core/pipeline/pipeline_builder.h"
#include <cstddef>
#include <gtest/gtest.h>
#include "steps/import_step.h"
#include "core/pipeline/pipeline.h"
#include "core/data/tile.h"
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core.hpp>
#include <memory>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <utility>

using Json = nlohmann::json;

namespace {
constexpr int INLIER_RATIO_DENOMINATOR = 5;
constexpr int TEXTURE_MINIMUM = 30;
constexpr int TEXTURE_SIZE = 576;
constexpr int TEXTURE_SEED = 2026;
constexpr int TEXTURE_MAXIMUM = 230;
constexpr double TEXTURE_SIGMA = 0.6;
constexpr double DEPTH_SCALE = 257.0;
constexpr float TEST_OVERLAP = 0.5f;
constexpr int MAXIMUM_FEATURES = 1000;
constexpr int TILE_WIDTH = 384;
constexpr int TILE_SHIFT = 192;
constexpr float EXPECTED_SHIFT = 192.f;
constexpr float TRANSLATION_TOLERANCE = 2.f;
constexpr int TILE_ID_A = 10;
constexpr int TILE_ID_B = 20;
constexpr int TILE_ID_C = 30;
constexpr int TILE_ID_D = 40;
constexpr unsigned long long OVERFLOW_COUNT = 4294967296ULL;
constexpr int EXCESSIVE_MATCH_COUNT = 1001;
constexpr double FRACTIONAL_FEATURE_COUNT = 4.5;
constexpr int BLANK_PIXEL = 100;
constexpr int MIN_MATCHES_PARAMETER_INDEX = 5;
constexpr int PARAMETER_COUNT = 6;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void runStep(StepsNs::SiftStepClass& step, const std::string& context) {
    const bool ok = step.execute();
    require(ok, context + ": " + step.getResult().dump());
}

struct Directory {
    std::filesystem::path path;
    Directory() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("sift-step-" + std::to_string(stamp));
        require(std::filesystem::create_directory(path), "create isolated test directory");
    }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

cv::Mat texture(int depth = CV_8U, bool color = false) {
    cv::Mat source(TEXTURE_SIZE, TEXTURE_SIZE, CV_8UC1);
    cv::RNG random(TEXTURE_SEED);
    random.fill(source, cv::RNG::UNIFORM, TEXTURE_MINIMUM, TEXTURE_MAXIMUM);
    cv::GaussianBlur(source, source, cv::Size(3, 3), TEXTURE_SIGMA);
    if (color) cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
    if (depth == CV_16U) source.convertTo(source, CV_16U, DEPTH_SCALE);
    return source;
}

std::shared_ptr<DataNs::TileClass> saveTile(const Directory& dir, const cv::Mat& source, cv::Point cell, int id) {
    const auto file = dir.path / ("img_C" + std::to_string(cell.x) + "_R" + std::to_string(cell.y) + ".tif");
    const cv::Mat image = source(cv::Rect(cell.x * TILE_SHIFT, cell.y * TILE_SHIFT, TILE_WIDTH, TILE_WIDTH)).clone();
    require(cv::imwrite(file.string(), image), "write TIFF fixture");
    return std::make_shared<DataNs::TileClass>(id, file, std::filesystem::path{}, image.cols, image.rows, image.type());
}

StepsNs::SiftInput parameters() {
    StepsNs::SiftInput input;
    input.overlap = TEST_OVERLAP;
    input.maxFeatures = MAXIMUM_FEATURES;
    input.claheClipLimit = 0.0;
    return input;
}

void checkTranslation(const DataNs::PairMatches& pair, float dx, float dy) {
    const auto& m = pair.featureMatches;
    require(m.pointsInA.size() >= 4 && m.pointsInA.size() == m.pointsInB.size() &&
            m.pointsInA.size() == m.confidenceScores.size(), "complete correspondence vectors");
    require(std::abs(m.translationX - dx) < TRANSLATION_TOLERANCE && std::abs(m.translationY - dy) < TRANSLATION_TOLERANCE, "translation in original tile coordinates");
    size_t correct = 0;
    for (size_t i = 0; i < m.pointsInA.size(); ++i) {
        const auto delta = m.pointsInA[i] - m.pointsInB[i];
        if (std::abs(delta.x - dx) < TRANSLATION_TOLERANCE && std::abs(delta.y - dy) < TRANSLATION_TOLERANCE) ++correct;
        require(std::isfinite(m.confidenceScores[i]) && m.confidenceScores[i] >= 0.f && m.confidenceScores[i] <= 1.f, "valid SIFT score");
    }
    require(correct * INLIER_RATIO_DENOMINATOR >= m.pointsInA.size() * 4, "at least 80% geometrically correct correspondences");
}

void horizontalAndPixelFormats() {
    for (const int depth : {CV_8U, CV_16U}) for (const bool color : {false, true}) {
        Directory dir;
        const auto source = texture(depth, color);
        auto a = saveTile(dir, source, {0, 0}, TILE_ID_A), b = saveTile(dir, source, {1, 0}, TILE_ID_B);
        a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
        b->setNeighbours(a.get(), nullptr, nullptr, nullptr);
        StepNs::DataStoreClass data;
        data.replace("tiles", DataNs::TileList{b, a});
        StepsNs::SiftStepClass step(parameters());
        step.setDataStore(data);
        runStep(step, "horizontal pair");
        const auto& pairs = data.get<std::vector<DataNs::PairMatches>>("matches");
        require(pairs.size() == 1 && pairs[0].tileIdA == TILE_ID_A && pairs[0].tileIdB == TILE_ID_B, "original tile ids");
        require(pairs[0].direction == DataNs::Direction::Right, "horizontal direction");
        checkTranslation(pairs[0], EXPECTED_SHIFT, 0.f);
        const Json serialized = step.getOutput();
        const Json decoded = Json::parse(serialized.dump());
        require(decoded.size() == 3 && decoded.at("key") == "matches" && decoded.at("type") == "PairMatches",
                "key/value/type envelope");
        const auto& first = decoded.at("value").at(0);
        require(first.at("tileIdA") == TILE_ID_A && first.at("tileIdB") == TILE_ID_B && first.at("direction") == "right", "pair identity JSON");
        require(first.at("matches").at("pointsInA").at(0).size() == 2, "JSON point coordinate pairs");
        require(step.getResult().at("pairs") == 1 && step.getResult().at("failures").empty() &&
                step.getResult().dump().find("pointsInA") == std::string::npos && !step.getResult().contains("value"),
                "result contains summary only");
    }
}

void verticalAndGrid() {
    Directory dir;
    const auto source = texture();
    auto a = saveTile(dir, source, {0, 0}, TILE_ID_A), b = saveTile(dir, source, {1, 0}, TILE_ID_B);
    auto c = saveTile(dir, source, {0, 1}, TILE_ID_C), d = saveTile(dir, source, {1, 1}, TILE_ID_D);
    a->setNeighbours(nullptr, b.get(), nullptr, c.get());
    b->setNeighbours(a.get(), nullptr, nullptr, d.get());
    c->setNeighbours(nullptr, d.get(), a.get(), nullptr);
    d->setNeighbours(c.get(), nullptr, b.get(), nullptr);
    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{d, b, a, c});
    StepsNs::SiftStepClass step(parameters());
    step.setDataStore(data);
    runStep(step, "grid");
    require(step.getOutput().value.size() == 4, "four unique grid pairs");
    std::set<std::pair<int, int>> seen;
    for (const auto& pair : step.getOutput().value) {
        require(seen.emplace(pair.tileIdA, pair.tileIdB).second, "no duplicate pair");
        const bool horizontal = pair.direction == DataNs::Direction::Right;
        checkTranslation(pair, horizontal ? EXPECTED_SHIFT : 0.f, horizontal ? 0.f : EXPECTED_SHIFT);
    }
}

void invalidInputs() {
    StepsNs::SiftStepClass step(parameters());
    require(!step.execute(), "unattached StepNs::DataStoreClass");
    StepNs::DataStoreClass data;
    step.setDataStore(data);
    require(!step.execute(), "missing tiles");
    data.replace("tiles", 3);
    require(!step.execute(), "wrong input type");
    auto a = std::make_shared<DataNs::TileClass>(1, "not-read.tif", "", TILE_WIDTH, TILE_WIDTH, CV_8UC1);
    auto b = std::make_shared<DataNs::TileClass>(2, "not-read.tif", "", TILE_WIDTH, TILE_WIDTH, CV_8UC1);
    data.replace("tiles", DataNs::TileList{a, nullptr});
    require(!step.execute(), "null tile");
    data.replace("tiles", DataNs::TileList{a, a});
    require(!step.execute(), "duplicate tile");
    auto duplicate = std::make_shared<DataNs::TileClass>(1, "not-read.tif", "", TILE_WIDTH, TILE_WIDTH, CV_8UC1);
    data.replace("tiles", DataNs::TileList{a, duplicate});
    require(!step.execute(), "duplicate ids");
    data.replace("tiles", DataNs::TileList{a, b});
    require(!step.execute(), "no neighbors");
    a->setNeighbours(nullptr, a.get(), nullptr, nullptr);
    require(!step.execute(), "self neighbor");
    a->setNeighbours(nullptr, duplicate.get(), nullptr, nullptr);
    require(!step.execute(), "neighbor outside DataNs::TileList");
    a->setNeighbours(nullptr, b.get(), nullptr, b.get());
    require(!step.execute(), "same neighbor as right and bottom");
    a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
    require(!step.execute(), "unreadable file");
    require(step.getOutput().value.empty() && data.get<std::vector<DataNs::PairMatches>>("matches").empty(), "no stale result");
}

void expectInvalidConfig(StepsNs::SiftStepClass& step, const Json& config) {
    bool rejected = false;
    try {
        step.importConfig(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid configuration must be rejected");
}

void checkInvalidSchema(StepsNs::SiftStepClass& step, const Json& original) {
    Json cfg = original;
    cfg["parameters"][1]["value"] = FRACTIONAL_FEATURE_COUNT;
    expectInvalidConfig(step, cfg);
    cfg = original;
    cfg["parameters"].push_back(cfg["parameters"][0]);
    expectInvalidConfig(step, cfg);
    cfg = original;
    cfg["parameters"][1]["valueType"] = "float";
    expectInvalidConfig(step, cfg);
    cfg = original;
    cfg["parameters"][1]["name"] = "max_features";
    expectInvalidConfig(step, cfg);
    StepsNs::SiftStepClass defaults;
    defaults.importConfig({{"name", "sift"}, {"parameters", {{{"valueType", "int"}, {"name", "maxFeatures"}, {"value", MAXIMUM_FEATURES}}}}});
    require(defaults.getConfigJson()["parameters"].size() == PARAMETER_COUNT, "missing parameters retain defaults");
}

void invalidOptions() {
    StepsNs::SiftStepClass step(parameters());
    StepNs::DataStoreClass data;
    step.setDataStore(data);
    const Json original = step.getConfigJson();
    const std::vector<std::pair<int, Json>> invalid = {
        {0, 0}, {0, 1}, {1, 0}, {1, OVERFLOW_COUNT}, {2, 0}, {2, 1},
        {3, -1}, {4, -1}, {MIN_MATCHES_PARAMETER_INDEX, 3}, {MIN_MATCHES_PARAMETER_INDEX, EXCESSIVE_MATCH_COUNT}
    };
    for (const auto& change : invalid) {
        Json cfg = original;
        cfg["parameters"][change.first]["value"] = change.second;
        step.importConfig(cfg);
        require(!step.execute() && step.getResult().contains("error"), "reject invalid domain value");
        const auto name = cfg.at("parameters").at(change.first).at("name").get<std::string>();
        require(step.getResult().at("error").get<std::string>().find(name) != std::string::npos,
                "invalid parameter named in diagnostic");
    }
    checkInvalidSchema(step, original);
}

void failureAndRetry() {
    Directory dir;
    const auto source = texture();
    auto a = saveTile(dir, source, {0, 0}, TILE_ID_A), b = saveTile(dir, source, {1, 0}, TILE_ID_B);
    a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
    StepNs::DataStoreClass data;
    data.replace("tiles", DataNs::TileList{a, b});
    StepsNs::SiftStepClass step(parameters());
    step.setDataStore(data);
    require(step.execute(), "initial success");
    auto missing = std::make_shared<DataNs::TileClass>(TILE_ID_C, dir.path / "absent.tif", "", TILE_WIDTH, TILE_WIDTH, CV_8UC1);
    b->setNeighbours(nullptr, missing.get(), nullptr, nullptr);
    data.replace("tiles", DataNs::TileList{a, b, missing});
    require(step.execute(), "later failed pair preserves earlier success");
    require(step.getResult().at("pairs") == 1 && step.getResult().at("failures").size() == 1 &&
            step.getResult().at("failures").at(0).at("idB") == TILE_ID_C, "failed pair identified");
    require(step.getOutput().value.size() == 1 && data.get<std::vector<DataNs::PairMatches>>("matches").size() == 1,
            "successful pair published");
    b->setNeighbours(nullptr, nullptr, nullptr, nullptr);
    data.replace("tiles", DataNs::TileList{a, b});
    require(step.execute() && !step.getResult().contains("error"), "clean retry");
    cv::Mat uniform(TILE_WIDTH, TILE_WIDTH, CV_8UC1, cv::Scalar(BLANK_PIXEL));
    require(cv::imwrite(a->getSource().string(), uniform) && cv::imwrite(b->getSource().string(), uniform), "write featureless fixtures");
    require(!step.execute() && step.getOutput().value.empty() && step.getResult()["failures"].size() == 1 &&
            step.getResult().contains("error") && data.get<std::vector<DataNs::PairMatches>>("matches").empty(),
            "all failed pairs clear stale results");
}

void importPipeline() {
    Directory dir;
    const auto source = texture(CV_16U);
    saveTile(dir, source, {0, 0}, TILE_ID_A);
    saveTile(dir, source, {1, 0}, TILE_ID_B);
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<StepsNs::ImportStepClass>(StepsNs::ImportInput{dir.path.string()}));
    pipeline.addStep(std::make_unique<StepsNs::SiftStepClass>(parameters()));
    require(pipeline.run(), "real StepsNs::ImportStepClass -> StepsNs::SiftStepClass pipeline");
    const auto& pairs = pipeline.getDataStore().get<std::vector<DataNs::PairMatches>>("matches");
    require(pairs.size() == 1 && pairs[0].tileIdA == 0 && pairs[0].tileIdB == 1, "import ids consumed");
    checkTranslation(pairs[0], EXPECTED_SHIFT, 0.f);
}
} // namespace

TEST(SiftStepTest, HorizontalFormatsAndSerialization) { horizontalAndPixelFormats(); }
TEST(SiftStepTest, VerticalAndGrid) { verticalAndGrid(); }
TEST(SiftStepTest, InvalidInputs) { invalidInputs(); }
TEST(SiftStepTest, InvalidParameters) { invalidOptions(); }
TEST(SiftStepTest, PartialFailureAndRetry) { failureAndRetry(); }
TEST(SiftStepTest, ImportPipeline) { importPipeline(); }
TEST(SiftStepTest, JsonRegistryAndRoundTrip) {
    const Json script = {{"pipeline", {{"steps", Json::array({Json{{"name", "import"}}, Json{{"name", "sift"},
        {"parameters", {{{"valueType", "int"}, {"name", "maxFeatures"}, {"value", MAXIMUM_FEATURES}}}}}})}}}};
    const auto pipeline = PipelineNs::buildPipeline(script);
    ASSERT_EQ(pipeline.getSteps().size(), 2);
    EXPECT_NE(dynamic_cast<StepsNs::SiftStepClass*>(pipeline.getSteps()[1].get()), nullptr);
    EXPECT_EQ(pipeline.getSteps()[1]->getConfigJson()["parameters"].size(), PARAMETER_COUNT);
    EXPECT_EQ(PipelineNs::buildPipeline(pipeline.exportConfig()).exportConfig(), pipeline.exportConfig());
}
