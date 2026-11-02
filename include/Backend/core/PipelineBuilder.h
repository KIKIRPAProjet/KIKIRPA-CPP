#pragma once
#include "core/Pipeline.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace stitching {

// Registre des steps : "name" du JSON → classe C++. Pour ajouter un step, une ligne dans registry()
// (PipelineBuilder.cpp) ; son constructeur par défaut fixe ses options, ses entrées et ses sorties.

// Noms des steps connus, dans l'ordre alphabétique
std::vector<std::string> registeredSteps();

// Crée un step avec sa config par défaut. Lève std::invalid_argument si le nom est inconnu.
std::unique_ptr<Step> createStep(const std::string& name);

// Construit le pipeline décrit par { "pipeline": { "steps": [...] } } (voir core/Config.h) : un step par entrée,
// dans l'ordre, créé d'après son "name", puis configuré par Step::importConfig.
// Lève std::invalid_argument si le JSON est mal formé, si un step est inconnu ou mal configuré, ou si une entrée
// d'un step (pipelineOptions) n'est produite par aucun step précédent avec le même type (signature).
Pipeline buildPipeline(const Json& script);

// Lit le fichier puis appelle buildPipeline. Lève std::runtime_error si le fichier est illisible ou si le JSON
// est mal formé, std::invalid_argument si le pipeline est invalide.
Pipeline buildPipelineFromFile(const std::filesystem::path& file);

} // namespace stitching
