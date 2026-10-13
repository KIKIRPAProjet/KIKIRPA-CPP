#pragma once
#include <pipeline/step_config_validator.hpp>
#include <functional>

namespace pipeline {
/** Chaque règle obligatoire exige une liaison directe explicite vers un type accepté
 * et, si précisé, un identifiant donné. Les règles optionnelles décrivent des
 * liaisons possibles sans jamais ajouter de dépendance implicite au graphe. */
struct DependencyRule {
    std::vector<std::string> types;
    std::optional<std::string> id;
    bool optional = false;
};
/** Ensemble des règles de dépendance, configuration et contrats pour un type d’étape. */
struct StepRules {
    std::vector<DependencyRule> dependencies;
    StepConfigValidator configuration;
    std::function<std::vector<Diagnostic>(const Step&)> customValidator;
    std::vector<std::string> outputs;
    // Une liste vide accepte toutes les sorties. Sinon, chaque producteur direct
    // doit fournir au moins un tag figurant parmi les entrées acceptées.
    std::vector<std::string> acceptedInputs;
};
/** Le registre contient uniquement les règles métier, jamais les fonctions d’exécution des étapes. */
class StepRegistry {
public:
    /** Enregistre un type ; un nom vide, un doublon ou une règle vide lève une exception. */
    void registerType(std::string type, StepRules rules = {});
    /** Renvoie les règles du type, ou nullptr si ce type n’est pas enregistré. */
    const StepRules* find(const std::string& type) const;
private:
    std::map<std::string, StepRules> rules_;
};
}
