// Checkpoint.cpp
#include "core/Checkpoint.h"
#include "core/Config.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stitching {

namespace {

struct Serializer {
    Checkpoint::Saver save;
    Checkpoint::Loader load;
};

std::map<std::string, Serializer>& serializers() {
    static std::map<std::string, Serializer> s;
    return s;
}

// Écriture atomique (fichier temporaire puis rename) : un crash ne laisse jamais un fichier tronqué
void writeAtomic(const fs::path& p, const std::string& content) {
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << content;
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    fs::rename(tmp, p);
}

Json readManifest(const fs::path& dir) {
    std::ifstream in(dir / "manifest.json", std::ios::binary);
    if (!in) return Json();
    const Json m = Json::parse(in, nullptr, false);   // fichier corrompu : pas d'exception, juste invalide
    return m.is_discarded() ? Json() : m;
}

} // namespace

void Checkpoint::registerType(const std::string& type, Saver save, Loader load) {
    serializers()[type] = {std::move(save), std::move(load)};
}

bool Checkpoint::hasType(const std::string& type) {
    return serializers().count(type) > 0;
}

fs::path Checkpoint::stepDir(size_t index, const std::string& name) const {
    char prefix[32];
    std::snprintf(prefix, sizeof prefix, "%02zu_", index);
    return root / (prefix + name);
}

bool Checkpoint::save(size_t index, const Step& step, const DataStore& data, std::string& why) const {
    if (!enabled()) return false;
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
    const Json manifest = {{"step", cfg.name}, {"options", step.getConfig().value("options", Json::array())},
                           {"outputs", outputs}, {"complete", complete}};
    writeAtomic(dir / "manifest.json", manifest.dump(2));   // en dernier
    return complete;
}

bool Checkpoint::valid(size_t index, const Step& step) const {
    if (!enabled()) return false;
    const std::string name = step.getConfig().value("name", "");
    const Json m = readManifest(stepDir(index, name));
    return m.is_object() && m.value("complete", false) && m.value("step", "") == name &&
           m.value("options", Json()) == step.getConfig().value("options", Json::array());   // options changées = périmé
}

void Checkpoint::load(size_t index, const Step& step, const std::string& key, const std::string& type,
                      DataStore& data) const {
    const fs::path dir = stepDir(index, step.getConfig().value("name", ""));
    const Json m = readManifest(dir);
    if (!m.is_object() || !m.contains("outputs")) throw std::runtime_error("missing or corrupted " + (dir / "manifest.json").string());
    // Une clé absente du manifest n'a pas été produite par le step : rien à restaurer
    for (const Json& o : m.at("outputs")) {
        if (o.value("key", "") != key) continue;
        const auto it = serializers().find(type);
        if (it == serializers().end()) throw std::runtime_error("no serializer for type '" + type + "'");
        it->second.load(data, key, dir);
        return;
    }
}

void Checkpoint::removeFrom(size_t from) const {
    if (!enabled() || !fs::is_directory(root)) return;
    for (const auto& e : fs::directory_iterator(root)) {
        const std::string n = e.path().filename().string();
        if (e.is_directory() && n.size() > 3 && n[2] == '_' && std::isdigit(static_cast<unsigned char>(n[0])) &&
            std::isdigit(static_cast<unsigned char>(n[1])) && static_cast<size_t>(std::stoi(n.substr(0, 2))) >= from)
            fs::remove_all(e.path());
    }
}

} // namespace stitching
