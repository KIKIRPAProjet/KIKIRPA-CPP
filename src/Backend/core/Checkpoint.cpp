#include "core/Checkpoint.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

namespace {

struct Serializer {
    std::function<void(const DataStore&, const std::string& key, const fs::path& dir)> save;
    std::function<void(DataStore&, const std::string& key, const fs::path& dir)> load;
};

std::string indexed(const std::string& key, size_t i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "_%04zu.tif", i);
    return key + buf;
}

// Tiles : un JSON de métadonnées + une image TIFF par tuile.
// TODO (optimisation) : pour import, référencer les fichiers sources au lieu de les recopier.
void saveTiles(const DataStore& d, const std::string& key, const fs::path& dir) {
    const Tiles& tiles = d.get<Tiles>(key);
    Json meta = Json::array();
    for (size_t i = 0; i < tiles.size(); ++i) {
        const std::string file = indexed(key, i);
        if (tiles[i].image.empty() || !cv::imwrite((dir / file).string(), tiles[i].image))
            throw std::runtime_error("cannot write " + (dir / file).string());
        meta.push_back({{"path", tiles[i].path.string()}, {"col", tiles[i].col}, {"row", tiles[i].row}, {"file", file}});
    }
    std::ofstream(dir / (key + ".json")) << meta.dump(2);
}

void loadTiles(DataStore& d, const std::string& key, const fs::path& dir) {
    std::ifstream in(dir / (key + ".json"));
    if (!in) throw std::runtime_error("missing " + (dir / (key + ".json")).string());
    Tiles tiles;
    for (const Json& m : Json::parse(in)) {
        TileImage t;
        t.path = m.at("path").get<std::string>();
        t.col = m.at("col").get<int>();
        t.row = m.at("row").get<int>();
        t.image = cv::imread((dir / m.at("file").get<std::string>()).string(), cv::IMREAD_UNCHANGED);
        if (t.image.empty()) throw std::runtime_error("cannot read checkpoint tile " + m.at("file").get<std::string>());
        tiles.push_back(std::move(t));
    }
    d.set(key, std::move(tiles));
}

// Type de PipelineOption (Config.h) -> (dé)sérialisation. Ajouter ici Alignments, Positions, Seams, Mosaic...
const std::map<std::string, Serializer>& serializers() {
    static const std::map<std::string, Serializer> s = {
        {"Tiles", {saveTiles, loadTiles}},
    };
    return s;
}

// Écriture atomique : un crash en plein milieu ne laisse jamais un fichier tronqué
void writeAtomic(const fs::path& p, const std::string& content) {
    const fs::path tmp = p.string() + ".tmp";
    { std::ofstream(tmp) << content; }
    fs::rename(tmp, p);
}

} // namespace

fs::path Checkpoint::stepDir(size_t index, const std::string& name) const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02zu_", index);
    return root / (buf + name);
}

bool Checkpoint::save(size_t index, const Step& step, const DataStore& data, std::string& why) const {
    const StepConfig cfg = step.getConfig().get<StepConfig>();
    const fs::path dir = stepDir(index, cfg.name);
    fs::remove_all(dir);
    fs::create_directories(dir);

    bool complete = true;
    Json outputs = Json::array();
    for (const PipelineOption& out : cfg.signature.pipelineOptions) {
        if (!data.has(out.key)) continue;   // le step n'a rien écrit pour cette clé : rien à sauver
        const auto it = serializers().find(out.type);
        if (it == serializers().end()) {
            why = "no serializer for type '" + out.type + "' (key '" + out.key + "')";
            complete = false;
            continue;
        }
        try {
            it->second.save(data, out.key, dir);
            outputs.push_back({{"key", out.key}, {"type", out.type}});
        } catch (const std::exception& e) {
            why = "cannot save '" + out.key + "': " + e.what();
            complete = false;
        }
    }
    const Json manifest = {{"step", cfg.name}, {"options", step.getConfig().at("options")},
                           {"outputs", outputs}, {"complete", complete}};
    writeAtomic(dir / "manifest.json", manifest.dump(2));   // en dernier
    return complete;
}

bool Checkpoint::valid(size_t index, const Step& step) const {
    const std::string name = step.getConfig().value("name", "");
    std::ifstream in(stepDir(index, name) / "manifest.json");
    if (!in) return false;
    const Json m = Json::parse(in, nullptr, false);   // pas d'exception si le fichier est corrompu
    if (m.is_discarded() || !m.is_object()) return false;
    return m.value("complete", false) && m.value("step", "") == name &&
           m.contains("options") && m.at("options") == step.getConfig().at("options");   // options changées = périmé
}

void Checkpoint::load(size_t index, const Step& step, const std::string& key, const std::string& type,
                      DataStore& data) const {
    const fs::path dir = stepDir(index, step.getConfig().at("name").get<std::string>());
    std::ifstream in(dir / "manifest.json");
    const Json m = Json::parse(in);
    // On ne restaure que ce que le manifest dit avoir sauvegardé (la signature peut déclarer des sorties
    // que le step n'a pas produites : clé absente du manifest = rien à restaurer)
    for (const Json& o : m.at("outputs")) {
        if (o.at("key") != key) continue;
        const auto it = serializers().find(type);
        if (it == serializers().end()) throw std::runtime_error("no serializer for type '" + type + "'");
        it->second.load(data, key, dir);
        return;
    }
}

void Checkpoint::removeFrom(size_t from) const {
    if (!fs::is_directory(root)) return;
    for (const auto& e : fs::directory_iterator(root)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 3 && n[2] == '_' && std::isdigit((unsigned char)n[0]) && std::isdigit((unsigned char)n[1]) &&
            static_cast<size_t>(std::stoi(n.substr(0, 2))) >= from)
            fs::remove_all(e.path());
    }
}

} // namespace stitching
