#pragma once

#include "core/Pipeline.h"
#include <filesystem>

namespace stitching {

// Lit un script { "pipeline": { "steps": [...] }, "run": {...} } et construit
// les steps configurés. Cette fonction ne lance pas l'exécution du pipeline.
// Lève std::runtime_error en cas d'erreur de fichier ou de syntaxe JSON et
// std::invalid_argument si le JSON ne décrit pas un pipeline valide.
Pipeline parsePipelineFile(const std::filesystem::path& filePath);

} // namespace stitching
