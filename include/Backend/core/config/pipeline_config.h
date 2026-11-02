// include/Backend/core/config/pipeline_config.h
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ConfigNs {

    using Json = nlohmann::json;

    // Format du script pipeline : { "pipeline": { "steps": [ ... ] } }
    // Les noms des champs = les clés JSON.

    // Paramètre réglable d'un step : { "type": "int", "key": "maxPoints", "value": 5000 }
    // Types autorisés : "string", "int", "float" (nombre réel, double en C++), "bool" (voir StepClass::importConfig).
    struct StepParameter {
        std::string valueType;
        std::string name;
        Json value;
    };

    // Donnée qui circule entre les steps : { "type": "Tiles", "key": "tiles" }
    struct DataSlot {
        std::string dataType;
        std::string key;
    };

    struct StepConfig {
        std::string name;
        std::vector<StepParameter> parameters;
        std::vector<DataSlot> inputs;    // entrées
        std::vector<DataSlot> outputs;   // sorties
    };

    struct PipelineConfig {
        std::vector<StepConfig> steps;
    };

    // Les tableaux absents du JSON sont lus comme vides
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StepParameter, valueType, name, value)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DataSlot, dataType, key)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StepConfig, name, parameters, inputs, outputs)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PipelineConfig, steps)

}
