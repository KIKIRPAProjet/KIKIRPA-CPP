// CheckpointTypes.cpp
#include "core/CheckpointTypes.h"
#include "core/Tuile.h"
#include "core/Types.h"
#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

namespace {

// ----- Fichier JSON d'une sortie : <dossier>/<clé>.json

void writeJson(const fs::path& dir, const std::string& key, const Json& value) {
    std::ofstream out(dir / (key + ".json"), std::ios::binary);
    out << value.dump();
    if (!out) throw std::runtime_error("cannot write " + (dir / (key + ".json")).string());
}

Json readJson(const fs::path& dir, const std::string& key) {
    std::ifstream in(dir / (key + ".json"), std::ios::binary);
    if (!in) throw std::runtime_error("missing " + (dir / (key + ".json")).string());
    return Json::parse(in);
}

// ----- Briques communes

Json point(const cv::Point2f& p) { return {p.x, p.y}; }
Json point(const cv::Point2d& p) { return {p.x, p.y}; }
Json point(const cv::Point& p) { return {p.x, p.y}; }

Json polyline(const std::vector<cv::Point>& pts) {
    Json a = Json::array();
    for (const auto& p : pts) a.push_back(point(p));
    return a;
}

std::vector<cv::Point> readPolyline(const Json& a) {
    std::vector<cv::Point> pts;
    for (const Json& p : a) pts.emplace_back(p.at(0).get<int>(), p.at(1).get<int>());
    return pts;
}

const char* directionName(Direction d) {
    switch (d) {
    case Direction::Top: return "top";
    case Direction::Bottom: return "bottom";
    case Direction::Left: return "left";
    case Direction::Right: return "right";
    }
    return "right";
}

Direction readDirection(const std::string& s) {
    if (s == "top") return Direction::Top;
    if (s == "bottom") return Direction::Bottom;
    if (s == "left") return Direction::Left;
    if (s == "right") return Direction::Right;
    throw std::runtime_error("unknown direction '" + s + "'");
}

// ----- "Tiles" : fiches seulement. Les voisins sont écrits par identifiant (-1 : pas de voisin).

int neighbourId(const Tuile* t) { return t ? t->getId() : -1; }

void saveTiles(const DataStore& d, const std::string& key, const fs::path& dir) {
    Json a = Json::array();
    for (const auto& t : d.get<Tiles>(key)) {
        const Niveau& n0 = t->getNiveau(0);
        a.push_back({{"id", t->getId()}, {"source", t->getSource().u8string()}, {"tiff", t->getTiff().u8string()},
                     {"width", n0.largeur}, {"height", n0.hauteur}, {"type", t->getType()},
                     {"left", neighbourId(t->getLeft())}, {"right", neighbourId(t->getRight())},
                     {"top", neighbourId(t->getTop())}, {"bottom", neighbourId(t->getBottom())}});
    }
    writeJson(dir, key, a);
}

void loadTiles(DataStore& d, const std::string& key, const fs::path& dir) {
    const Json a = readJson(dir, key);
    Tiles tiles;
    std::map<int, Tuile*> byId;
    for (const Json& j : a) {
        const fs::path tiff = fs::u8path(j.at("tiff").get<std::string>());
        // Le checkpoint ne recopie pas les pixels : le TIFF pyramidal doit toujours exister
        if (!tiff.empty() && !fs::exists(tiff)) throw std::runtime_error("missing pyramidal TIFF " + tiff.string());
        tiles.push_back(std::make_shared<Tuile>(j.at("id").get<int>(), fs::u8path(j.at("source").get<std::string>()),
                                                tiff, j.at("width").get<int>(), j.at("height").get<int>(),
                                                j.at("type").get<int>()));
        byId[tiles.back()->getId()] = tiles.back().get();
    }
    const auto find = [&](const Json& id) -> Tuile* {
        const auto it = byId.find(id.get<int>());
        return it == byId.end() ? nullptr : it->second;
    };
    for (size_t i = 0; i < tiles.size(); ++i) {
        const Json& j = a.at(i);
        tiles[i]->setVoisins(find(j.at("left")), find(j.at("right")), find(j.at("top")), find(j.at("bottom")));
    }
    d.set(key, std::move(tiles));
}

// ----- "PairMatches" : std::vector<PairMatches>

void savePairMatches(const DataStore& d, const std::string& key, const fs::path& dir) {
    Json a = Json::array();
    for (const PairMatches& p : d.get<std::vector<PairMatches>>(key)) {
        Json pa = Json::array(), pb = Json::array();
        for (const auto& pt : p.matches.pointsA) pa.push_back(point(pt));
        for (const auto& pt : p.matches.pointsB) pb.push_back(point(pt));
        a.push_back({{"idA", p.idA}, {"idB", p.idB}, {"dir", directionName(p.dir)},
                     {"pointsA", pa}, {"pointsB", pb}, {"scores", p.matches.scores},
                     {"dx", p.matches.dx}, {"dy", p.matches.dy}});
    }
    writeJson(dir, key, a);
}

void loadPairMatches(DataStore& d, const std::string& key, const fs::path& dir) {
    std::vector<PairMatches> pairs;
    for (const Json& j : readJson(dir, key)) {
        PairMatches p;
        p.idA = j.at("idA").get<int>();
        p.idB = j.at("idB").get<int>();
        p.dir = readDirection(j.at("dir").get<std::string>());
        for (const Json& pt : j.at("pointsA")) p.matches.pointsA.emplace_back(pt.at(0).get<float>(), pt.at(1).get<float>());
        for (const Json& pt : j.at("pointsB")) p.matches.pointsB.emplace_back(pt.at(0).get<float>(), pt.at(1).get<float>());
        p.matches.scores = j.at("scores").get<std::vector<float>>();
        p.matches.dx = j.at("dx").get<float>();
        p.matches.dy = j.at("dy").get<float>();
        pairs.push_back(std::move(p));
    }
    d.set(key, std::move(pairs));
}

// ----- "Positions" : std::vector<TilePositionData>

void savePositions(const DataStore& d, const std::string& key, const fs::path& dir) {
    Json a = Json::array();
    for (const TilePositionData& p : d.get<std::vector<TilePositionData>>(key))
        a.push_back({{"tile_id", p.tile_id}, {"position", point(p.position)}, {"rotation", p.rotation}});
    writeJson(dir, key, a);
}

void loadPositions(DataStore& d, const std::string& key, const fs::path& dir) {
    std::vector<TilePositionData> positions;
    for (const Json& j : readJson(dir, key))
        positions.push_back({j.at("tile_id").get<std::string>(),
                             cv::Point2d(j.at("position").at(0).get<double>(), j.at("position").at(1).get<double>()),
                             j.at("rotation").get<double>()});
    d.set(key, std::move(positions));
}

// ----- "Territories" : std::vector<Seamline::TileTerritory>

void saveTerritories(const DataStore& d, const std::string& key, const fs::path& dir) {
    Json a = Json::array();
    for (const Seamline::TileTerritory& t : d.get<std::vector<Seamline::TileTerritory>>(key)) {
        Json polygons = Json::array(), holes = Json::array();
        for (const auto& poly : t.polygons) polygons.push_back(polyline(poly));
        for (const auto& hs : t.holes) {
            Json h = Json::array();
            for (const auto& hole : hs) h.push_back(polyline(hole));
            holes.push_back(h);
        }
        a.push_back({{"tile_index", t.tile_index}, {"polygons", polygons}, {"holes", holes}});
    }
    writeJson(dir, key, a);
}

void loadTerritories(DataStore& d, const std::string& key, const fs::path& dir) {
    std::vector<Seamline::TileTerritory> territories;
    for (const Json& j : readJson(dir, key)) {
        Seamline::TileTerritory t;
        t.tile_index = j.at("tile_index").get<int>();
        for (const Json& poly : j.at("polygons")) t.polygons.push_back(readPolyline(poly));
        for (const Json& hs : j.at("holes")) {
            std::vector<std::vector<cv::Point>> h;
            for (const Json& hole : hs) h.push_back(readPolyline(hole));
            t.holes.push_back(std::move(h));
        }
        territories.push_back(std::move(t));
    }
    d.set(key, std::move(territories));
}

} // namespace

std::map<std::string, std::pair<Checkpoint::Saver, Checkpoint::Loader>> pipelineTypes() {
    return {
        {"Tiles", {saveTiles, loadTiles}},
        {"PairMatches", {savePairMatches, loadPairMatches}},
        {"Positions", {savePositions, loadPositions}},
        {"Territories", {saveTerritories, loadTerritories}},
    };
}

} // namespace stitching
