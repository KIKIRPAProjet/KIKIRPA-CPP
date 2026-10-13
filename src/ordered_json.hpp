#pragma once
#include <pipeline/step_registry.hpp>

namespace pipeline::detail {
/** Convertit name/options en modèle interne, en conservant l’ordre de la liste. */
Json normalizeOrdered(const Json&, const StepRegistry&, ValidationReport&);
/** Valide les options typées et les transforme en objet de configuration. */
Json readOptions(const Json&, const std::string&, ValidationReport&);
/** Enregistre les noms et paramètres du format métier name/options. */
void registerOrderedTypes(StepRegistry&);
}
