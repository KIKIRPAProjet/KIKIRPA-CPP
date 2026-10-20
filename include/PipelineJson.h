#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

// Création du JSON d'un pipeline, à appeler depuis le frontend.
//
// Modèle (voir le diagramme) :
//   Pipeline 1--* Step
//   Step     1--* Option         { type, key, value }
//   Step     1--* PipelineOption { type, key }   (entrées)
//   Step     1--1 Signature                      (sorties : PipelineOption)
namespace pipeline_json {

// Valeur d'une option : texte, entier, flottant ou booléen
using OptionValue = std::variant<std::string, long long, double, bool>;

// Paramètre réglable d'un step
struct Option {
    std::string type;   // "string", "int", "float", "bool"
    std::string key;
    OptionValue value;

    Option(std::string type, std::string key, const char* value);
    Option(std::string type, std::string key, std::string value);
    Option(std::string type, std::string key, int value);
    Option(std::string type, std::string key, long long value);
    Option(std::string type, std::string key, double value);
    Option(std::string type, std::string key, bool value);
};

// Donnée qui circule entre les steps
struct PipelineOption {
    std::string type;
    std::string key;
};

struct Step {
    std::string name;
    std::vector<Option> options;
    std::vector<PipelineOption> inputs;    // entrées du step
    std::vector<PipelineOption> outputs;   // Signature : sorties du step

    explicit Step(std::string name) : name(std::move(name)) {}

    Step& addOption(Option option);
    Step& addInput(PipelineOption input);
    Step& addOutput(PipelineOption output);
};

struct Pipeline {
    std::vector<Step> steps;

    Pipeline& addStep(Step step);
};

// Construit une Option à partir d'un texte saisi (champ de formulaire, par exemple)
// en le convertissant selon `type` ("string", "int", "float", "bool").
// Lève std::invalid_argument si la valeur ne correspond pas au type ou si le type est inconnu.
Option optionFromText(const std::string& type, const std::string& key, const std::string& text);

// Retourne le JSON du pipeline sous forme de texte (indent < 0 : une seule ligne).
std::string toJson(const Pipeline& pipeline, int indent = 2);

// Écrit le JSON du pipeline dans un fichier (crée les dossiers manquants).
// Lève std::runtime_error si l'écriture échoue.
void writeJson(const Pipeline& pipeline, const std::filesystem::path& file, int indent = 2);

} // namespace pipeline_json
