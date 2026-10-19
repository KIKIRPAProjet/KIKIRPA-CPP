#pragma once
#include "core/Pipeline.h"
#include <memory>
#include <string>
#include <vector>

namespace stitching {

// Construit le pipeline décrit par { "pipeline": { "steps": [...] } } (voir core/Config.h) :
// un step par entrée, dans l'ordre, créé d'après son "name".
// Lève std::invalid_argument si un nom de step est inconnu ou si une entrée d'un step
// (pipelineOptions) n'est produite par aucun step précédent (signature).
Pipeline buildPipeline(const Json& script);

// Noms des steps connus, dans l'ordre d'un pipeline complet (utilisé par le générateur de pipeline.json)
const std::vector<std::string>& defaultStepOrder();

// Crée un step avec sa config par défaut. Lève std::invalid_argument si le nom est inconnu.
std::unique_ptr<Step> createStep(const std::string& name);

} // namespace stitching
