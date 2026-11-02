#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace stitching {

// Format du script pipeline : { "pipeline": { "steps": [ ... ] } }
// Les noms des champs = les clés JSON.

// Paramètre réglable d'un step : { "type": "int", "key": "maxPoints", "value": 5000 }
// Types autorisés : "string", "int", "float" (nombre réel, double en C++), "bool" (voir Step::importConfig).
struct Option {
    std::string type;
    std::string key;
    Json value;
};

// Donnée qui circule entre les steps : { "type": "Tiles", "key": "tiles" }
struct PipelineOption {
    std::string type;
    std::string key;
};

// Ce que le step produit
struct Signature {
    std::vector<PipelineOption> pipelineOptions;
};

struct StepConfig {
    std::string name;
    std::vector<Option> options;
    std::vector<PipelineOption> pipelineOptions;   // entrées
    Signature signature;                           // sorties
};

struct PipelineConfig {
    std::vector<StepConfig> steps;
};

// Les tableaux absents du JSON sont lus comme vides
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Option, type, key, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PipelineOption, type, key)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Signature, pipelineOptions)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StepConfig, name, options, pipelineOptions, signature)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PipelineConfig, steps)

} // namespace stitching
