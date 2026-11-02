#include "steps/sift_step.h"
#include "core/pipeline/pipeline_builder.h"
#include <gtest/gtest.h>
#include "steps/import_step.h"
#include "core/pipeline/pipeline.h"
#include "core/data/tile.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace DataNs;
using namespace StepsNs;
using Json = nlohmann::json;

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void runStep(SiftStepClass& step, const std::string& context) {
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
    cv::Mat source(576, 576, CV_8UC1);
    cv::RNG random(2026);
    random.fill(source, cv::RNG::UNIFORM, 30, 230);
    cv::GaussianBlur(source, source, cv::Size(3, 3), 0.6);
    if (color) cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
    if (depth == CV_16U) source.convertTo(source, CV_16U, 257.0);
    return source;
}

std::shared_ptr<TileClass> saveTile(const Directory& dir, const cv::Mat& source, int col, int row, int id) {
    const auto file = dir.path / ("img_C" + std::to_string(col) + "_R" + std::to_string(row) + ".tif");
    const cv::Mat image = source(cv::Rect(col * 192, row * 192, 384, 384)).clone();
    require(cv::imwrite(file.string(), image), "write TIFF fixture");
    return std::make_shared<TileClass>(id, file, std::filesystem::path{}, image.cols, image.rows, image.type());
}

SiftInput parameters() {
    SiftInput input;
    input.overlap = 0.5f;
    input.maxFeatures = 1000;
    input.claheClipLimit = 0.0;
    return input;
}

void checkTranslation(const PairMatches& pair, float dx, float dy) {
    const auto& m = pair.featureMatches;
    require(m.pointsInA.size() >= 4 && m.pointsInA.size() == m.pointsInB.size() &&
            m.pointsInA.size() == m.confidenceScores.size(), "complete correspondence vectors");
    require(std::abs(m.translationX - dx) < 2.f && std::abs(m.translationY - dy) < 2.f, "translation in original tile coordinates");
    size_t correct = 0;
    for (size_t i = 0; i < m.pointsInA.size(); ++i) {
        const auto delta = m.pointsInA[i] - m.pointsInB[i];
        if (std::abs(delta.x - dx) < 2.f && std::abs(delta.y - dy) < 2.f) ++correct;
        require(std::isfinite(m.confidenceScores[i]) && m.confidenceScores[i] >= 0.f && m.confidenceScores[i] <= 1.f, "valid SIFT score");
    }
    require(correct * 5 >= m.pointsInA.size() * 4, "at least 80% geometrically correct correspondences");
}

void horizontalAndPixelFormats() {
    for (const int depth : {CV_8U, CV_16U}) for (const bool color : {false, true}) {
        Directory dir;
        const auto source = texture(depth, color);
        auto a = saveTile(dir, source, 0, 0, 10), b = saveTile(dir, source, 1, 0, 20);
        a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
        b->setNeighbours(a.get(), nullptr, nullptr, nullptr);
        StepNs::DataStoreClass data;
        data.replace("tiles", TileList{b, a});
        SiftStepClass step(parameters());
        step.setDataStore(data);
        runStep(step, "horizontal pair");
        const auto& pairs = data.get<std::vector<PairMatches>>("matches");
        require(pairs.size() == 1 && pairs[0].tileIdA == 10 && pairs[0].tileIdB == 20, "original tile ids");
        require(pairs[0].direction == Direction::Right, "horizontal direction");
        checkTranslation(pairs[0], 192.f, 0.f);
        const Json serialized = step.getOutput();
        const Json decoded = Json::parse(serialized.dump());
        require(decoded.size() == 3 && decoded.at("key") == "matches" && decoded.at("type") == "PairMatches",
                "key/value/type envelope");
        const auto& first = decoded.at("value").at(0);
        require(first.at("tileIdA") == 10 && first.at("tileIdB") == 20 && first.at("direction") == "right", "pair identity JSON");
        require(first.at("matches").at("pointsInA").at(0).size() == 2, "JSON point coordinate pairs");
        require(step.getResult().at("pairs") == 1 && step.getResult().at("failures").empty() &&
                step.getResult().dump().find("pointsInA") == std::string::npos && !step.getResult().contains("value"),
                "result contains summary only");
    }
}

void verticalAndGrid() {
    Directory dir;
    const auto source = texture();
    auto a = saveTile(dir, source, 0, 0, 10), b = saveTile(dir, source, 1, 0, 20);
    auto c = saveTile(dir, source, 0, 1, 30), d = saveTile(dir, source, 1, 1, 40);
    a->setNeighbours(nullptr, b.get(), nullptr, c.get());
    b->setNeighbours(a.get(), nullptr, nullptr, d.get());
    c->setNeighbours(nullptr, d.get(), a.get(), nullptr);
    d->setNeighbours(c.get(), nullptr, b.get(), nullptr);
    StepNs::DataStoreClass data;
    data.replace("tiles", TileList{d, b, a, c});
    SiftStepClass step(parameters());
    step.setDataStore(data);
    runStep(step, "grid");
    require(step.getOutput().value.size() == 4, "four unique grid pairs");
    std::set<std::pair<int, int>> seen;
    for (const auto& pair : step.getOutput().value) {
        require(seen.emplace(pair.tileIdA, pair.tileIdB).second, "no duplicate pair");
        const bool horizontal = pair.direction == Direction::Right;
        checkTranslation(pair, horizontal ? 192.f : 0.f, horizontal ? 0.f : 192.f);
    }
}

void invalidInputs() {
    SiftStepClass step(parameters());
    require(!step.execute(), "unattached StepNs::DataStoreClass");
    StepNs::DataStoreClass data;
    step.setDataStore(data);
    require(!step.execute(), "missing tiles");
    data.replace("tiles", 3);
    require(!step.execute(), "wrong input type");
    auto a = std::make_shared<TileClass>(1, "not-read.tif", "", 384, 384, CV_8UC1);
    auto b = std::make_shared<TileClass>(2, "not-read.tif", "", 384, 384, CV_8UC1);
    data.replace("tiles", TileList{a, nullptr});
    require(!step.execute(), "null tile");
    data.replace("tiles", TileList{a, a});
    require(!step.execute(), "duplicate tile");
    auto duplicate = std::make_shared<TileClass>(1, "not-read.tif", "", 384, 384, CV_8UC1);
    data.replace("tiles", TileList{a, duplicate});
    require(!step.execute(), "duplicate ids");
    data.replace("tiles", TileList{a, b});
    require(!step.execute(), "no neighbors");
    a->setNeighbours(nullptr, a.get(), nullptr, nullptr);
    require(!step.execute(), "self neighbor");
    a->setNeighbours(nullptr, duplicate.get(), nullptr, nullptr);
    require(!step.execute(), "neighbor outside TileList");
    a->setNeighbours(nullptr, b.get(), nullptr, b.get());
    require(!step.execute(), "same neighbor as right and bottom");
    a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
    require(!step.execute(), "unreadable file");
    require(step.getOutput().value.empty() && data.get<std::vector<PairMatches>>("matches").empty(), "no stale result");
}

void invalidOptions() {
    SiftStepClass step(parameters());
    StepNs::DataStoreClass data;
    step.setDataStore(data);
    const Json original = step.getConfigJson();
    const std::vector<std::pair<int, Json>> invalid = {
        {0, 0}, {0, 1}, {1, 0}, {1, 4294967296ULL}, {2, 0}, {2, 1},
        {3, -1}, {4, -1}, {5, 3}, {5, 1001}
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
    const auto reject = [&](Json cfg) {
        EXPECT_THROW(step.importConfig(cfg), std::invalid_argument);
    };
    Json cfg = original;
    cfg["parameters"][1]["value"] = 4.5;
    reject(cfg);
    cfg = original;
    cfg["parameters"].push_back(cfg["parameters"][0]);
    reject(cfg);
    cfg = original;
    cfg["parameters"][1]["valueType"] = "float";
    reject(cfg);
    cfg = original;
    cfg["parameters"][1]["name"] = "max_features";
    reject(cfg);
    SiftStepClass defaults;
    defaults.importConfig({{"name", "sift"}, {"parameters", {{{"valueType", "int"}, {"name", "maxFeatures"}, {"value", 1000}}}}});
    require(defaults.getConfigJson()["parameters"].size() == 6, "missing parameters retain defaults");
}

void failureAndRetry() {
    Directory dir;
    const auto source = texture();
    auto a = saveTile(dir, source, 0, 0, 10), b = saveTile(dir, source, 1, 0, 20);
    a->setNeighbours(nullptr, b.get(), nullptr, nullptr);
    StepNs::DataStoreClass data;
    data.replace("tiles", TileList{a, b});
    SiftStepClass step(parameters());
    step.setDataStore(data);
    require(step.execute(), "initial success");
    auto missing = std::make_shared<TileClass>(30, dir.path / "absent.tif", "", 384, 384, CV_8UC1);
    b->setNeighbours(nullptr, missing.get(), nullptr, nullptr);
    data.replace("tiles", TileList{a, b, missing});
    require(step.execute(), "later failed pair preserves earlier success");
    require(step.getResult().at("pairs") == 1 && step.getResult().at("failures").size() == 1 &&
            step.getResult().at("failures").at(0).at("idB") == 30, "failed pair identified");
    require(step.getOutput().value.size() == 1 && data.get<std::vector<PairMatches>>("matches").size() == 1,
            "successful pair published");
    b->setNeighbours(nullptr, nullptr, nullptr, nullptr);
    data.replace("tiles", TileList{a, b});
    require(step.execute() && !step.getResult().contains("error"), "clean retry");
    cv::Mat uniform(384, 384, CV_8UC1, cv::Scalar(100));
    require(cv::imwrite(a->getSource().string(), uniform) && cv::imwrite(b->getSource().string(), uniform), "write featureless fixtures");
    require(!step.execute() && step.getOutput().value.empty() && step.getResult()["failures"].size() == 1 &&
            step.getResult().contains("error") && data.get<std::vector<PairMatches>>("matches").empty(),
            "all failed pairs clear stale results");
}

void importPipeline() {
    Directory dir;
    const auto source = texture(CV_16U);
    saveTile(dir, source, 0, 0, 10);
    saveTile(dir, source, 1, 0, 20);
    PipelineNs::PipelineClass pipeline;
    pipeline.addStep(std::make_unique<ImportStepClass>(ImportInput{dir.path.string()}));
    pipeline.addStep(std::make_unique<SiftStepClass>(parameters()));
    require(pipeline.run(), "real ImportStepClass -> SiftStepClass pipeline");
    const auto& pairs = pipeline.getDataStore().get<std::vector<PairMatches>>("matches");
    require(pairs.size() == 1 && pairs[0].tileIdA == 0 && pairs[0].tileIdB == 1, "import ids consumed");
    checkTranslation(pairs[0], 192.f, 0.f);
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
        {"parameters", {{{"valueType", "int"}, {"name", "maxFeatures"}, {"value", 1000}}}}}})}}}};
    const auto pipeline = PipelineNs::buildPipeline(script);
    ASSERT_EQ(pipeline.getSteps().size(), 2);
    EXPECT_NE(dynamic_cast<SiftStepClass*>(pipeline.getSteps()[1].get()), nullptr);
    EXPECT_EQ(pipeline.getSteps()[1]->getConfigJson()["parameters"].size(), 6);
    EXPECT_EQ(PipelineNs::buildPipeline(pipeline.exportConfig()).exportConfig(), pipeline.exportConfig());
}
