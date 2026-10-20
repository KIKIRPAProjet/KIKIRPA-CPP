#include "PipelineJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace pipeline_json {

using Json = nlohmann::json;

// ---------- Option ----------

Option::Option(std::string t, std::string k, const char* v)
    : type(std::move(t)), key(std::move(k)), value(std::string(v)) {}
Option::Option(std::string t, std::string k, std::string v)
    : type(std::move(t)), key(std::move(k)), value(std::move(v)) {}
Option::Option(std::string t, std::string k, int v)
    : type(std::move(t)), key(std::move(k)), value(static_cast<long long>(v)) {}
Option::Option(std::string t, std::string k, long long v)
    : type(std::move(t)), key(std::move(k)), value(v) {}
Option::Option(std::string t, std::string k, double v)
    : type(std::move(t)), key(std::move(k)), value(v) {}
Option::Option(std::string t, std::string k, bool v)
    : type(std::move(t)), key(std::move(k)), value(v) {}

// ---------- Step / Pipeline ----------

Step& Step::addOption(Option option) { options.push_back(std::move(option)); return *this; }
Step& Step::addInput(PipelineOption input) { inputs.push_back(std::move(input)); return *this; }
Step& Step::addOutput(PipelineOption output) { outputs.push_back(std::move(output)); return *this; }

Pipeline& Pipeline::addStep(Step step) { steps.push_back(std::move(step)); return *this; }

// ---------- Conversion texte -> Option ----------

Option optionFromText(const std::string& type, const std::string& key, const std::string& text) {
    const auto invalid = [&]() {
        return std::invalid_argument("valeur '" + text + "' invalide pour l'option '" + key + "' de type " + type);
    };

    if (type == "string") return Option(type, key, text);

    if (type == "int") {
        try {
            size_t used = 0;
            const long long v = std::stoll(text, &used);
            if (used != text.size()) throw invalid();
            return Option(type, key, v);
        } catch (const std::logic_error&) {   // invalid_argument / out_of_range de stoll
            throw invalid();
        }
    }

    if (type == "float") {
        try {
            size_t used = 0;
            const double v = std::stod(text, &used);
            if (used != text.size()) throw invalid();
            return Option(type, key, v);
        } catch (const std::logic_error&) {
            throw invalid();
        }
    }

    if (type == "bool") {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
        if (t == "true" || t == "1" || t == "yes" || t == "on") return Option(type, key, true);
        if (t == "false" || t == "0" || t == "no" || t == "off") return Option(type, key, false);
        throw invalid();
    }

    throw std::invalid_argument("type d'option non supporté '" + type + "' (string, int, float, bool)");
}

// ---------- Création du JSON ----------

namespace {

Json toJson(const PipelineOption& p) {
    Json j;
    j["type"] = p.type;
    j["key"] = p.key;
    return j;
}

Json toJson(const Option& o) {
    Json j;
    j["type"] = o.type;
    j["key"] = o.key;
    j["value"] = std::visit([](const auto& v) { return Json(v); }, o.value);
    return j;
}

Json toJson(const Step& s) {
    Json j;
    j["name"] = s.name;

    j["options"] = Json::array();
    for (const auto& o : s.options) j["options"].push_back(toJson(o));

    j["pipelineOptions"] = Json::array();
    for (const auto& p : s.inputs) j["pipelineOptions"].push_back(toJson(p));

    j["signature"]["pipelineOptions"] = Json::array();
    for (const auto& p : s.outputs) j["signature"]["pipelineOptions"].push_back(toJson(p));

    return j;
}

} // namespace

std::string toJson(const Pipeline& pipeline, int indent) {
    Json root;
    root["pipeline"]["steps"] = Json::array();
    for (const auto& s : pipeline.steps) root["pipeline"]["steps"].push_back(toJson(s));
    return root.dump(indent);
}

void writeJson(const Pipeline& pipeline, const std::filesystem::path& file, int indent) {
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());

    std::ofstream out(file);
    out << toJson(pipeline, indent) << '\n';
    if (!out) throw std::runtime_error("impossible d'écrire " + file.string());
}

} // namespace pipeline_json
