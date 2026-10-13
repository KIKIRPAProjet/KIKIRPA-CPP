#pragma once
#include <pipeline/step.hpp>
#include <map>

namespace pipeline {
/** Contraintes d’un paramètre : type, présence, bornes inclusives et valeurs autorisées. */
struct ConfigField {
    enum class Type { String, Number, Integer, Boolean, Object, Array };
    Type type = Type::String;
    bool required = false;
    std::optional<double> minimum, maximum;
    std::vector<Json> allowed;
};
/** Validateur déclaratif réutilisable, complétable par une fonction de validation spécifique. */
class StepConfigValidator {
public:
    std::map<std::string, ConfigField> fields;
    bool allowUnknown = true;
    /** Vérifie les champs de configuration déclarés et les éventuels champs inconnus. */
    std::vector<Diagnostic> validate(const Step& step) const;
};
}
