#include "steps/ImportStep.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <regex>

namespace fs = std::filesystem;

namespace stitching::steps {

ImportStep::ImportStep(const ImportInput& input) {
    StepConfig cfg;
    cfg.name = "import";
    cfg.options = {{"string", "path", input.path}, {"string", "format", input.format}};
    cfg.signature.pipelineOptions = {{"Tiles", keys::Tiles}};
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
    std::set<std::pair<int, int>> coordinates;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        std::smatch m;
        if (!entry.is_regular_file() || !std::regex_match(name, m, format)) continue;
        int col = 0;
        int row = 0;
        try {
            col = std::stoi(m[1].str());
            row = std::stoi(m[2].str());
        } catch (const std::exception& e) {
            outputData["error"] = "invalid grid coordinate in '" + name + "': " + e.what();
            return false;
        }
        if (col < 0 || row < 0) {
            outputData["error"] = "negative grid coordinate in '" + name + "'";
            return false;
        }
        if (!coordinates.emplace(col, row).second) {
            outputData["error"] = "duplicate tile coordinate (" + std::to_string(col) + ", " +
                                  std::to_string(row) + ")";
            return false;
        }
        tiles.push_back({entry.path(), col, row, {}});
    }

    if (tiles.empty()) {
        outputData["error"] = "no file matching '" + input.format + "' in " + dir.string();
        return false;
    }

    // Ligne par ligne, puis colonne par colonne
    std::sort(tiles.begin(), tiles.end(), [](const TileImage& a, const TileImage& b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    
    int minCol = tiles.front().col;
    int maxCol = tiles.front().col;
    int minRow = tiles.front().row;
    int maxRow = tiles.front().row;
    for (const TileImage& tile : tiles) {
        minCol = std::min(minCol, tile.col);
        maxCol = std::max(maxCol, tile.col);
        minRow = std::min(minRow, tile.row);
        maxRow = std::max(maxRow, tile.row);
    }
    const std::uint64_t gridWidth =
        static_cast<std::uint64_t>(maxCol) - static_cast<std::uint64_t>(minCol) + 1;
    const std::uint64_t gridHeight =
        static_cast<std::uint64_t>(maxRow) - static_cast<std::uint64_t>(minRow) + 1;
    if (gridWidth * gridHeight != coordinates.size()) {
        outputData["error"] = "tile grid is not a complete rectangle";
        return false;
    }
    for (std::int64_t row = minRow; row <= maxRow; ++row) {
        for (std::int64_t col = minCol; col <= maxCol; ++col) {
            if (!coordinates.count({static_cast<int>(col), static_cast<int>(row)})) {
                outputData["error"] = "missing tile at grid coordinate (" +
                                      std::to_string(col) + ", " + std::to_string(row) + ")";
                return false;
            }
        }
    }
    
    for (auto& t : tiles) {
        t.image = cv::imread(t.path.string(), cv::IMREAD_UNCHANGED);   // garde le 16 bits
        if (t.image.empty()) {
            outputData["error"] = "cannot read " + t.path.string();
            return false;
        }
    }

    output.tiles = std::move(tiles);
    outputData["tiles"] = output.tiles.size();
    if (data) data->set(keys::Tiles, output.tiles);   // pour les steps suivants (copie légère : cv::Mat partagé)
    return true;
}

} // namespace stitching::steps
