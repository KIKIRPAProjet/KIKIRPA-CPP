#pragma once
#include <pipeline/step.hpp>
#include <map>

namespace pipeline {
/** Résultat interne du graphe ; seul un rapport global valide publie son ordre. */
struct Resolution {
    std::vector<Diagnostic> errors;
    std::vector<std::string> order;
};
/** Le parcours en profondeur itératif détecte les cycles. L’algorithme de Kahn
 * calcule l’ordre d’exécution, avec un départage lexicographique des identifiants. */
class DependencyResolver {
public:
    /** Analyse les liaisons, produit les diagnostics et calcule l’ordre topologique. */
    Resolution resolve(const std::map<std::string, Step>& steps) const;
};
}
