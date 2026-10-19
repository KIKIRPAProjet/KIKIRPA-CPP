#include "core/PipelineBuilder.h"
#include "core/Config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace stitching;

// Générateur de pipeline.json (ébauche).
// Les steps connaissent leurs options et leurs valeurs par défaut : on les instancie, on applique les arguments de la
// console, on valide l'enchaînement avec buildPipeline() (même contrôle que l'exécution), puis on écrit le JSON.
//
//   generate_pipeline [options] [step.option=valeur ...]
//     --list              affiche les steps, leurs options, leurs types et leurs valeurs par défaut
//     -i, --interactive   demande chaque option dans la console (Entrée = garder la valeur proposée)
//     --steps a,b,c       ne garde que ces steps, dans cet ordre (défaut : tous)
//     -o <fichier>        fichier de sortie (défaut : pipeline.json à la racine du projet)
//   ex. : generate_pipeline seamline.seamFinder=graphcut stitching.ratio=0.8 export.path=output/mosaic.tif

namespace {

struct StepEntry {
    std::string name;
    std::vector<Option> options;   // valeurs courantes
    std::vector<Option> defaults;  // valeurs par défaut (pour repérer ce qui a été modifié)
};

std::string show(const Json& v) { return v.dump(); }

// Convertit le texte de la console selon le type déclaré de l'option ("int", "float", "bool", "string")
Json parseValue(const Option& o, const std::string& text) {
    try {
        size_t used = 0;
        if (o.type == "string") return text;
        if (o.type == "int") {
            const long long v = std::stoll(text, &used);
            if (used != text.size()) throw std::invalid_argument("");
            return v;
        }
        if (o.type == "float") {
            const double v = std::stod(text, &used);
            if (used != text.size()) throw std::invalid_argument("");
            return v;
        }
        if (o.type == "bool") {
            std::string t = text;
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
            if (t == "true" || t == "1" || t == "yes" || t == "on") return true;
            if (t == "false" || t == "0" || t == "no" || t == "off") return false;
            throw std::invalid_argument("");
        }
    } catch (const std::exception&) {
        throw std::invalid_argument("'" + text + "' is not a valid " + o.type + " for option '" + o.key + "'");
    }
    throw std::invalid_argument("option '" + o.key + "' has unsupported type '" + o.type + "'");
}

Option* findOption(StepEntry& s, const std::string& key) {
    for (Option& o : s.options)
        if (o.key == key) return &o;
    return nullptr;
}

std::string keysOf(const StepEntry& s) {
    std::string r;
    for (const Option& o : s.options) r += (r.empty() ? "" : ", ") + o.key;
    return r;
}

void printStep(const StepEntry& s, size_t i, size_t n) {
    std::cout << "[" << i + 1 << "/" << n << "] " << s.name << '\n';
    for (size_t k = 0; k < s.options.size(); ++k) {
        const Option& o = s.options[k];
        std::cout << "    " << s.name << '.' << o.key << "  (" << o.type << ")  default " << show(s.defaults[k].value) << '\n';
    }
}

void usage() {
    std::cout << "Usage: generate_pipeline [-o fichier] [-i] [--steps a,b,c] [--list] [step.option=valeur ...]\n"
                 "  ex.  generate_pipeline seamline.seamFinder=graphcut stitching.ratio=0.8\n"
                 "       generate_pipeline --list\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string out = std::string(PROJECT_DIR) + "/pipeline.json";
    std::vector<std::string> stepNames = defaultStepOrder(), overrides;
    bool interactive = false, list = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--list") list = true;
        else if (a == "-i" || a == "--interactive") interactive = true;
        else if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--steps" && i + 1 < argc) {
            stepNames.clear();
            std::stringstream ss(argv[++i]);
            for (std::string n; std::getline(ss, n, ',');) stepNames.push_back(n);
        } else if (a.find('=') != std::string::npos && a[0] != '-') overrides.push_back(a);
        else { std::cerr << "unknown argument '" << a << "'\n"; usage(); return 1; }
    }

    try {
        // 1. Un step par nom, avec ses options par défaut
        std::vector<StepEntry> steps;
        for (const std::string& n : stepNames) {
            const StepConfig cfg = createStep(n)->getConfig().get<StepConfig>();
            steps.push_back({n, cfg.options, cfg.options});
        }
        if (list) {
            for (size_t i = 0; i < steps.size(); ++i) printStep(steps[i], i, steps.size());
            return 0;
        }

        // 2. Arguments de la console : step.option=valeur
        for (const std::string& ov : overrides) {
            const size_t eq = ov.find('=');   // 1er '=' : la valeur peut en contenir (regex, chemins...)
            const std::string lhs = ov.substr(0, eq), value = ov.substr(eq + 1);
            const size_t dot = lhs.find('.');
            if (dot == std::string::npos)
                throw std::invalid_argument("'" + ov + "': expected step.option=value (ex. seamline.seamFinder=graphcut)");
            const std::string sName = lhs.substr(0, dot), key = lhs.substr(dot + 1);
            const auto st = std::find_if(steps.begin(), steps.end(), [&](const StepEntry& s) { return s.name == sName; });
            if (st == steps.end()) throw std::invalid_argument("'" + ov + "': step '" + sName + "' is not in the pipeline");
            Option* o = findOption(*st, key);
            if (!o) throw std::invalid_argument("step '" + sName + "' has no option '" + key + "' (available: " + keysOf(*st) + ")");
            o->value = parseValue(*o, value);
        }

        // 3. Mode interactif : on propose chaque option, Entrée garde la valeur courante
        if (interactive) {
            for (size_t i = 0; i < steps.size(); ++i) {
                std::cout << "\n[" << i + 1 << "/" << steps.size() << "] " << steps[i].name << '\n';
                for (Option& o : steps[i].options) {
                    for (;;) {
                        std::cout << "  " << o.key << " (" << o.type << ") [" << show(o.value) << "]: " << std::flush;
                        std::string line;
                        if (!std::getline(std::cin, line)) throw std::runtime_error("input closed, nothing written");
                        if (line.empty()) break;
                        try { o.value = parseValue(o, line); break; }
                        catch (const std::invalid_argument& e) { std::cout << "  " << e.what() << '\n'; }
                    }
                }
            }
            std::cout << '\n';
        }

        // 4. Le script { "pipeline": { "steps": [ { name, options } ] } }
        Json jsteps = Json::array();
        for (const StepEntry& s : steps) {
            Json opts = Json::array();
            for (const Option& o : s.options) opts.push_back(o);
            jsteps.push_back({{"name", s.name}, {"options", opts}});
        }
        const Json script = {{"pipeline", {{"steps", jsteps}}}};

        // 5. Validation avant d'écrire : mêmes contrôles que l'exécution (steps/options connus, enchaînement des données)
        buildPipeline(script);

        if (std::filesystem::path(out).has_parent_path()) std::filesystem::create_directories(std::filesystem::path(out).parent_path());
        std::ofstream file(out);
        file << script.dump(2) << '\n';
        if (!file) throw std::runtime_error("cannot write " + out);

        // 6. Récapitulatif : ce qui diffère des valeurs par défaut est marqué d'une étoile
        for (size_t i = 0; i < steps.size(); ++i) {
            std::cout << i + 1 << ". " << steps[i].name;
            for (size_t k = 0; k < steps[i].options.size(); ++k) {
                const Option& o = steps[i].options[k];
                std::cout << "  " << o.key << '=' << show(o.value) << (o.value != steps[i].defaults[k].value ? "*" : "");
            }
            std::cout << '\n';
        }
        std::cout << "(* = différent de la valeur par défaut)\nÉcrit : " << out << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "generate_pipeline: " << e.what() << '\n';
        return 1;
    }
}
