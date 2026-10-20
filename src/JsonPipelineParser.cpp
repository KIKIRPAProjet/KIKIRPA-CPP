#include "core/JsonPipelineParser.h"
#include "core/PipelineBuilder.h"

#include <fstream>
#include <stdexcept>

namespace stitching {

Pipeline parsePipelineFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        throw std::runtime_error("Impossible d'ouvrir le fichier : " + filePath.string());
    }

    Json script;
    try {
        script = Json::parse(file);
    } catch (const Json::parse_error& e) {
        throw std::runtime_error(
            "JSON mal formé dans '" + filePath.string() + "' : " + e.what());
    }

    return buildPipeline(script);
}

} // namespace stitching
