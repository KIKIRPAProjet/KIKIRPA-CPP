#pragma once
#include <pipeline/step_registry.hpp>

namespace pipeline {
/** Coordonne les vérifications statiques sans exécuter les étapes du pipeline. */
class PipelineValidator {
public:
    /** Possède une copie du registre ; les pipelines vides sont refusés par défaut. */
    explicit PipelineValidator(StepRegistry registry, bool allowEmpty = false);
    /** Valide la structure, les règles métier et le graphe d’un document déjà interprété. */
    ValidationReport validate(const Json& document) const;
    /** Interprète le texte JSON et convertit les erreurs de lecture en diagnostics. */
    ValidationReport validateText(const std::string& text) const;
    /** Lit un fichier puis valide son contenu, sans accéder aux données métier référencées. */
    ValidationReport validateFile(const std::string& path) const;
private:
    StepRegistry registry_;
    bool allowEmpty_;
    std::map<std::string, Step> readSteps(const Json&, ValidationReport&) const;
    void validateRules(const std::map<std::string, Step>&, ValidationReport&) const;
};
/** Garde d’intégration : lève une exception si le document est invalide.
 * À appeler immédiatement avant l’exécution du même document immuable. */
ValidationReport requireValid(const PipelineValidator&, const Json&);
}
