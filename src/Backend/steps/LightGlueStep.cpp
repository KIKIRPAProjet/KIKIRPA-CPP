#include "steps/LightGlueStep.h"
#include "core/Config.h"
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

} // namespace

void to_json(Json& json, const MatchedTile& tile) {
    json = {{"index", tile.index}, {"path", tile.path.string()}, {"col", tile.col}, {"row", tile.row}};
}

void to_json(Json& json, const TilePairMatches& pair) {
    const char* direction;
    switch (pair.direction) {
    case Direction::Right: direction = "right"; break;
    case Direction::Bottom: direction = "bottom"; break;
    case Direction::Left: direction = "left"; break;
    case Direction::Top: direction = "top"; break;
    default: throw std::invalid_argument("unknown pair direction");
    }
    Json pointsA = Json::array(), pointsB = Json::array();
    for (const auto& point : pair.matches.pointsA) pointsA.push_back({point.x, point.y});
    for (const auto& point : pair.matches.pointsB) pointsB.push_back({point.x, point.y});
    json = {{"a", pair.a}, {"b", pair.b}, {"direction", direction},
            {"matches", {{"pointsA", std::move(pointsA)}, {"pointsB", std::move(pointsB)},
                         {"scores", pair.matches.scores}, {"dx", pair.matches.dx}, {"dy", pair.matches.dy}}}};
}

void to_json(Json& json, const LightGlueOutput& output) {
    json = {{"key", output.key}, {"value", output.value}, {"type", output.type}};
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
        const auto& tiles = data->get<Tiles>("tiles");
        if (tiles.size() < 2) throw std::invalid_argument("at least two tiles are required");

        std::map<std::pair<int, int>, size_t> grid;
        for (size_t i = 0; i < tiles.size(); ++i) {
            const auto& t = tiles[i];
            if (t.col < 0 || t.row < 0) throw std::invalid_argument("negative tile coordinates");
            if (!grid.emplace(std::make_pair(t.row, t.col), i).second)
                throw std::invalid_argument("duplicate grid position");
            if (t.image.empty() || t.image.dims != 2)
                throw std::invalid_argument("empty or non-2D tile: " + t.path.string());
            if ((t.image.depth() != CV_8U && t.image.depth() != CV_16U) ||
                (t.image.channels() != 1 && t.image.channels() != 3 && t.image.channels() != 4))
                throw std::invalid_argument("expected 8/16-bit tile with 1, 3 or 4 channels: " + t.path.string());
        }

        struct Pair { size_t a, b; Direction dir; };
        std::vector<Pair> pairs;
        for (const auto& entry : grid) {
            const int row = entry.first.first, col = entry.first.second;
            const auto append = [&](int r, int c, Direction dir) {
                const auto neighbor = grid.find({r, c});
                if (neighbor != grid.end()) pairs.push_back({entry.second, neighbor->second, dir});
            };
            if (col < std::numeric_limits<int>::max()) append(row, col + 1, Direction::Right);
            if (row < std::numeric_limits<int>::max()) append(row + 1, col, Direction::Bottom);
        }
        if (pairs.empty()) throw std::invalid_argument("no horizontally or vertically adjacent tiles");
        if (!matcher || loadedModel != model) {
            auto next = factory(Json{{"model_path", model}});
            if (!next) throw std::runtime_error("matcher factory returned null");
            matcher = std::move(next);
            loadedModel = model;
        }

        PairMatches pending;
        for (const auto& p : pairs) {
            const auto& a = tiles[p.a];
            const auto& b = tiles[p.b];
            pair = "(" + std::to_string(a.col) + "," + std::to_string(a.row) + ") -> (" +
                   std::to_string(b.col) + "," + std::to_string(b.row) + ")";
            Matches matches;
            if (!matcher->match(a.image, b.image, {p.dir, static_cast<float>(overlap)}, matches))
                throw std::runtime_error("matcher could not find correspondences");
            validateMatches(matches, a.image, b.image, minimum);
            pending.push_back({{p.a, a.path, a.col, a.row}, {p.b, b.path, b.col, b.row},
                               p.dir, std::move(matches)});
        }
        LightGlueOutput completed;
        completed.value = std::move(pending);
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
