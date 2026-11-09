// include/backend/core/config/pipeline_config.h
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ConfigNs {

    using Json = nlohmann::json;

    // Configuration d'un pipeline, telle qu'écrite dans le script JSON : { "pipeline": { "steps": [ ... ] } }.
    // Le nom de chaque champ C++ est sa clé JSON.

    // Réglage d'un step, ex. { "valueType": "int", "name": "maxPoints", "value": 5000 }.
    // valueType : "string", "int", "float" (double en C++), "bool" ou "json" (tableau ou objet, dont le step vérifie
    // le contenu) ; value doit y correspondre (voir StepClass::importConfig).
    struct StepParameter {
        std::string valueType;
        std::string name;
        Json value;
    };

    // Donnée échangée entre steps, ex. { "dataType": "Tiles", "key": "tiles" } :
    // dataType = type de la donnée, key = clé sous laquelle elle est rangée dans le DataStoreClass.
    struct DataSlot {
        std::string dataType;
        std::string key;
    };

    // Configuration d'un step : name (nom dans le registre des steps), parameters (réglages),
    // inputs (données lues, produites par un step précédent), outputs (données produites).
    struct StepConfig {
        std::string name;
        std::vector<StepParameter> parameters;
        std::vector<DataSlot> inputs;
        std::vector<DataSlot> outputs;
    };

    // Steps dans l'ordre d'exécution.
    struct PipelineConfig {
        std::vector<StepConfig> steps;
    };

    // Conversion JSON automatique. Avec _WITH_DEFAULT, une clé absente garde la valeur par défaut (tableau vide) ;
    // sans, elle est obligatoire.
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StepParameter, valueType, name, value)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DataSlot, dataType, key)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StepConfig, name, parameters, inputs, outputs)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PipelineConfig, steps)

}
