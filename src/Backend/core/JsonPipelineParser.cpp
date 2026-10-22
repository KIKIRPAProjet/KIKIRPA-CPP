#include "core/JsonPipelineParser.h"
#include "core/PipelineBuilder.h"
#include <fstream>
#include <stdexcept>
#include <string>

namespace stitching {

Pipeline parsePipelineFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Impossible d'ouvrir le fichier : " + filePath.string());
    }

    Json script;
    try {
        script = Json::parse(file);
    } catch (const Json::parse_error& e) {
        throw std::runtime_error(
            "JSON mal formé dans '" + filePath.string() + "' (octet " +
            std::to_string(e.byte) + ") : " + e.what());
    }
    if (file.bad()) {
        throw std::runtime_error("Erreur de lecture du fichier : " + filePath.string());
    }

    try {
        return buildPipeline(script);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(
            "Configuration de pipeline invalide dans '" + filePath.string() + "' : " + e.what());
    }
    return buildPipeline(script);
}

} // namespace stitching
