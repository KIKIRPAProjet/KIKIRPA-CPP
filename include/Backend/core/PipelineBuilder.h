#pragma once
#include "core/Pipeline.h"

namespace stitching {

// Construit le pipeline décrit par { "pipeline": { "steps": [...] } } (voir core/Config.h) :
// un step par entrée, dans l'ordre, créé d'après son "name".
// Lève std::invalid_argument si un nom de step est inconnu ou si une entrée d'un step
// (pipelineOptions) n'est produite par aucun step précédent (signature).
Pipeline buildPipeline(const Json& script);

} // namespace stitching
