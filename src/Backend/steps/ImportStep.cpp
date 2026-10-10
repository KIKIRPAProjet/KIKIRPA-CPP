#include "steps/ImportStep.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace stitching::steps {

ImportStep::ImportStep(const ImportInput& input) {
    StepConfig cfg;
    cfg.name = "import";
    cfg.options = {{"string", "path", input.path}, {"string", "format", input.format}};
    cfg.signature.pipelineOptions = {{"Tiles", "tiles"}};
    config = cfg;
}

bool ImportStep::execute() {
    // Relu depuis config : un JSON chargé par Pipeline::configure() remplace les valeurs du constructeur
    const ImportInput input{option("path").get<std::string>(), option("format").get<std::string>()};
    const fs::path dir = input.path;

    if (!fs::is_directory(dir)) {
        outputData["error"] = "not a directory: " + dir.string();
        return false;
    }

    std::regex format;
    try {
        format = std::regex(input.format, std::regex::icase);   // .tif / .TIF
    } catch (const std::regex_error& e) {
        outputData["error"] = "invalid format '" + input.format + "': " + e.what();
        return false;
    }
    if (format.mark_count() < 2) {
        outputData["error"] = "format '" + input.format + "' needs 2 groups: (column) and (row)";
        return false;
    }

    // Fichiers dont le nom correspond au format ; groupe 1 = colonne, groupe 2 = ligne
    Tiles tiles;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        std::smatch m;
        if (!entry.is_regular_file() || !std::regex_match(name, m, format)) continue;
        tiles.push_back({entry.path(), std::stoi(m[1].str()), std::stoi(m[2].str()), {}});
    }

    if (tiles.empty()) {
        outputData["error"] = "no file matching '" + input.format + "' in " + dir.string();
        return false;
    }

    // Ligne par ligne, puis colonne par colonne
    std::sort(tiles.begin(), tiles.end(), [](const TileImage& a, const TileImage& b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });

    for (auto& t : tiles) {
        t.image = cv::imread(t.path.string(), cv::IMREAD_UNCHANGED);   // garde le 16 bits
        if (t.image.empty()) {
            outputData["error"] = "cannot read " + t.path.string();
            return false;
        }
    }

    output.tiles = std::move(tiles);
    outputData["tiles"] = output.tiles.size();
    if (data) data->set("tiles", output.tiles);   // pour les steps suivants (copie légère : cv::Mat partagé)
    return true;
}

} // namespace stitching::steps
