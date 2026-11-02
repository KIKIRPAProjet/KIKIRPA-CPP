#include "steps/LightGlueStep.h"
#include "core/Config.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <set>
#if defined(STITCHING_HAS_LIGHTGLUE)
#include "algorithms/stitching/lightglue/LightGlueMatcher.h"
#endif
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace stitching::steps {
namespace {

std::unique_ptr<matching::FeatureMatcher> createMatcher(const Json& params) {
#if defined(STITCHING_HAS_LIGHTGLUE)
    return std::make_unique<matching::LightGlueMatcher>(params);
#else
    (void)params;
    throw std::runtime_error("LightGlue unavailable: rebuild with ONNX Runtime and OpenCV dnn");
#endif
}

bool validPoint(const cv::Point2f& p, const cv::Mat& image) {
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.f && p.y >= 0.f &&
           p.x < image.cols && p.y < image.rows;
}

void validateMatches(const Matches& m, const cv::Mat& a, const cv::Mat& b, size_t minimum) {
    if (m.pointsA.size() != m.pointsB.size() || m.pointsA.size() != m.scores.size())
        throw std::runtime_error("inconsistent point and score counts");
    if (m.pointsA.size() < minimum)
        throw std::runtime_error("not enough correspondences: " + std::to_string(m.pointsA.size()) +
                                 ", expected at least " + std::to_string(minimum));
    if (!std::isfinite(m.dx) || !std::isfinite(m.dy))
        throw std::runtime_error("non-finite translation");
    for (size_t i = 0; i < m.pointsA.size(); ++i) {
        if (!validPoint(m.pointsA[i], a) || !validPoint(m.pointsB[i], b))
            throw std::runtime_error("correspondence outside image bounds");
        if (!std::isfinite(m.scores[i]) || m.scores[i] < 0.9f || m.scores[i] > 1.f)
            throw std::runtime_error("invalid confidence score (expected 0.9 to 1)");
    }
}

struct NeighborPair { const Tuile* a; const Tuile* b; Direction direction; };

std::vector<NeighborPair> collectPairs(const Tiles& tiles) {
    if (tiles.size() < 2) throw std::invalid_argument("at least two tiles are required");
    std::set<const Tuile*> members;
    std::set<int> ids;
    for (const auto& tile : tiles) {
        if (!tile) throw std::invalid_argument("null tile");
        if (!members.insert(tile.get()).second || !ids.insert(tile->getId()).second)
            throw std::invalid_argument("duplicate tile or tile id");
        if (tile->getId() < 0) throw std::invalid_argument("negative tile id");
    }
    std::vector<NeighborPair> pairs;
    std::set<std::pair<int, int>> seen;
    for (const auto& tile : tiles) {
        const std::pair<const Tuile*, Direction> neighbors[] = {
            {tile->getRight(), Direction::Right}, {tile->getBottom(), Direction::Bottom}
        };
        for (const auto& neighbor : neighbors) {
            if (!neighbor.first) continue;
            if (neighbor.first == tile.get() || !members.count(neighbor.first))
                throw std::invalid_argument("neighbor must be a distinct tile from the input Tiles");
            const auto edge = std::minmax({tile->getId(), neighbor.first->getId()});
            if (!seen.emplace(edge.first, edge.second).second)
                throw std::invalid_argument("duplicate or cyclic neighbor pair");
            pairs.push_back({tile.get(), neighbor.first, neighbor.second});
        }
    }
    if (pairs.empty()) throw std::invalid_argument("no right or bottom neighbor pairs");
    return pairs;
}

cv::Mat readImage(const Tuile& tile) {
    auto image = cv::imread(tile.getSource().string(), cv::IMREAD_UNCHANGED);
    if (image.empty()) throw std::runtime_error("cannot read " + tile.getSource().string());
    if (image.dims != 2 || (image.depth() != CV_8U && image.depth() != CV_16U) ||
        (image.channels() != 1 && image.channels() != 3 && image.channels() != 4))
        throw std::invalid_argument("expected a 2D 8/16-bit image with 1, 3 or 4 channels");
    return image;
}

} // namespace

void to_json(Json& json, const LightGlueOutput& output) {
    Json pairs = Json::array();
    for (const auto& pair : output.value) {
        if (pair.dir != Direction::Right && pair.dir != Direction::Bottom)
            throw std::invalid_argument("LightGlueOutput expects right or bottom pairs");
        Json pointsA = Json::array(), pointsB = Json::array();
        for (const auto& point : pair.matches.pointsA) pointsA.push_back({point.x, point.y});
        for (const auto& point : pair.matches.pointsB) pointsB.push_back({point.x, point.y});
        pairs.push_back({{"idA", pair.idA}, {"idB", pair.idB},
                         {"direction", pair.dir == Direction::Right ? "right" : "bottom"},
                         {"matches", {{"pointsA", std::move(pointsA)}, {"pointsB", std::move(pointsB)},
                                      {"scores", pair.matches.scores}, {"dx", pair.matches.dx}, {"dy", pair.matches.dy}}}});
    }
    json = {{"key", output.key}, {"value", std::move(pairs)}, {"type", output.type}};
}

LightGlueStep::LightGlueStep(const LightGlueInput& input, MatcherFactory f)
    : factory(f ? std::move(f) : MatcherFactory(createMatcher)) {
    StepConfig cfg;
    cfg.name = "lightglue";
    cfg.options = {{"string", "model_path", input.modelPath},
                   {"int", "min_matches", input.minMatches},
                   {"float", "overlap", input.overlap}};
    cfg.pipelineOptions = {{"Tiles", "tiles"}};
    cfg.signature.pipelineOptions = {{output.type, output.key}};
    config = cfg;
}

bool LightGlueStep::execute() {
    output = LightGlueOutput{};
    outputData = Json::object();
    std::string pair;
    try {
        if (!data) throw std::runtime_error("DataStore is not attached");
        // Une exécution en échec ne doit pas laisser une sortie antérieure accessible.
        data->erase(output.key);
        const auto cfg = config.get<StepConfig>();
        if (cfg.name != "lightglue") throw std::invalid_argument("expected step name 'lightglue'");
        std::map<std::string, Json> options;
        for (const auto& opt : cfg.options) {
            const std::string expected = opt.key == "model_path" ? "string" :
                                         opt.key == "min_matches" ? "int" :
                                         opt.key == "overlap" ? "float" : "";
            if (expected.empty() || opt.type != expected)
                throw std::invalid_argument("unknown option or incorrect type: " + opt.key);
            if (!options.emplace(opt.key, opt.value).second)
                throw std::invalid_argument("duplicate option: " + opt.key);
        }
        if (options.size() != 3) throw std::invalid_argument("model_path, min_matches and overlap are required");
        const auto& pathValue = options.at("model_path");
        const auto& minimumValue = options.at("min_matches");
        const auto& overlapValue = options.at("overlap");
        if (!pathValue.is_string() || pathValue.get<std::string>().empty())
            throw std::invalid_argument("model_path must be a non-empty string");
        if (!minimumValue.is_number_integer() || minimumValue < 1 ||
            minimumValue > std::numeric_limits<int>::max())
            throw std::invalid_argument("min_matches must be a positive integer");
        if (!overlapValue.is_number()) throw std::invalid_argument("overlap must be numeric");
        const double overlap = overlapValue.get<double>();
        if (!std::isfinite(overlap) || overlap <= 0.0 || overlap > 1.0)
            throw std::invalid_argument("overlap must be in (0, 1]");
        const std::string model = pathValue.get<std::string>();
        const auto minimum = static_cast<size_t>(minimumValue.get<int>());
        const auto pairs = collectPairs(data->get<Tiles>("tiles"));
        LightGlueOutput completed;
        completed.value.reserve(pairs.size());
        cv::Mat a;
        const Tuile* loadedA = nullptr;
        for (const auto& p : pairs) {
            pair = std::to_string(p.a->getId()) + " -> " + std::to_string(p.b->getId());
            if (loadedA != p.a) {
                a = readImage(*p.a);
                loadedA = p.a;
            }
            const cv::Mat b = readImage(*p.b);
            if (!matcher || loadedModel != model) {
                auto next = factory(Json{{"model_path", model}});
                if (!next) throw std::runtime_error("matcher factory returned null");
                matcher = std::move(next);
                loadedModel = model;
            }
            PairMatches result{p.a->getId(), p.b->getId(), p.direction, {}};
            if (!matcher->match(a, b, {p.direction, static_cast<float>(overlap), std::nullopt}, result.matches))
                throw std::runtime_error("matcher could not find correspondences");
            validateMatches(result.matches, a, b, minimum);
            completed.value.push_back(std::move(result));
        }
        Json exported = completed;
        data->set(completed.key, completed.value);
        output = std::move(completed);
        outputData = std::move(exported);
        return true;
    } catch (const std::exception& e) {
        outputData = {{"error", e.what()}};
        if (!pair.empty()) outputData["pair"] = pair;
        return false;
    }
}

} // namespace stitching::steps
