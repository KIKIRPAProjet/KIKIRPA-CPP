#pragma once

#include "core/Pipeline.h"
#include <filesystem>

namespace stitching {

// Lit un fichier JSON et construit le pipeline qu'il décrit.
Pipeline parsePipelineFile(const std::filesystem::path& filePath);

} // namespace stitching
