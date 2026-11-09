// src/backend/steps/import_step.cpp
#include "steps/import_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_writer.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <filesystem>
#include <exception>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace fs = std::filesystem; // NOLINT(readability-identifier-naming)

    namespace {

        struct ImageFile {
            fs::path filePath;
            int column;
            int row;
        };

        using TileByGridPosition = std::map<std::pair<int, int>, DataNs::TileClass*>;

        // Keep path, grid-position and file checks together so invalid tiles cannot be partially accepted.
        // NOLINTNEXTLINE(readability-function-size)
        bool appendExplicitTile(const Json& tile, std::set<fs::path>& paths,
                                std::set<std::pair<int, int>>& positions,
                                std::vector<ImageFile>& imageFiles, std::string& error) {
            if (!tile.is_object() || !tile.contains("path") || !tile["path"].is_string() ||
                !tile.contains("row") || !tile["row"].is_number_integer() ||
                !tile.contains("column") || !tile["column"].is_number_integer()) {
                error = "each explicit tile needs string path and integer row/column";
                return false;
            }
            const fs::path path = tile["path"].get<std::string>();
            const int row = tile["row"].get<int>();
            const int column = tile["column"].get<int>();
            if (row < 0 || column < 0 || !fs::is_regular_file(path)) {
                error = "explicit tile path is missing or its position is invalid: " + path.string();
                return false;
            }
            if (!paths.insert(fs::absolute(path).lexically_normal()).second ||
                !positions.insert({row, column}).second) {
                error = "explicit tile list contains duplicate paths or positions";
                return false;
            }
            imageFiles.push_back({path, column, row});
            return true;
        }

        // This helper validates both supported import modes and returns a single actionable error.
        // NOLINTNEXTLINE(readability-function-size)
        bool collectImageFiles(const ImportInput& input, const fs::path& directory,
                              const Json& explicitTiles, std::vector<ImageFile>& imageFiles,
                              std::string& error) {
            if (!explicitTiles.is_array()) {
                error = "explicit tile list must be a JSON array";
                return false;
            }
            if (!explicitTiles.empty()) {
                std::set<fs::path> paths;
                std::set<std::pair<int, int>> positions;
                for (const Json& tile : explicitTiles)
                    if (!appendExplicitTile(tile, paths, positions, imageFiles, error))
                        return false;
                return true;
            }
            std::regex pattern;
            try {
                pattern = std::regex(input.format, std::regex::icase);
            } catch (const std::regex_error& exception) {
                error = "invalid format '" + input.format + "': " + exception.what();
                return false;
            }
            if (pattern.mark_count() < 2) {
                error = "format '" + input.format + "' needs 2 groups: (column) and (row)";
                return false;
            }
            for (const auto& entry : fs::directory_iterator(directory)) {
                const std::string fileName = entry.path().filename().string();
                std::smatch match;
                if (entry.is_regular_file() && std::regex_match(fileName, match, pattern))
                    imageFiles.push_back({entry.path(), std::stoi(match[1].str()),
                                          std::stoi(match[2].str())});
            }
            if (imageFiles.empty())
                error = "no file matching '" + input.format + "' in " + directory.string();
            return !imageFiles.empty();
        }

        // Reading, converting and publishing one tile is kept atomic for the caller.
        // NOLINTNEXTLINE(readability-function-size)
        bool appendPyramidTile(const ImageFile& imageFile, bool explicitTiles,
                               const fs::path& outputDirectory, DataNs::TileList& tiles,
                               TileByGridPosition& tileByGridPosition, std::string& error) {
            const cv::Mat image = cv::imread(imageFile.filePath.string(), cv::IMREAD_UNCHANGED);
            if (image.empty()) {
                error = "cannot read " + imageFile.filePath.string();
                return false;
            }
            std::string pyramidName = imageFile.filePath.stem().string();
            if (explicitTiles)
                pyramidName += "_r" + std::to_string(imageFile.row) + "_c" +
                               std::to_string(imageFile.column);
            pyramidName += "_pyramid.tif";
            const fs::path pyramidPath = outputDirectory / pyramidName;
            std::string writerError;
            if (!IONs::createPyramidalTiledTiff(image, pyramidPath, writerError)) {
                error = "cannot create " + pyramidPath.string() + ": " + writerError;
                return false;
            }
            tiles.push_back(std::make_shared<DataNs::TileClass>(
                static_cast<int>(tiles.size()), imageFile.filePath, pyramidPath,
                image.cols, image.rows, image.type()));
            tileByGridPosition[{imageFile.column, imageFile.row}] = tiles.back().get();
            return true;
        }

        void linkTileNeighbours(const TileByGridPosition& tileByGridPosition) {
            auto neighbourAt = [&tileByGridPosition](int column, int row) -> DataNs::TileClass* {
                const auto entry = tileByGridPosition.find({column, row});
                return entry == tileByGridPosition.end() ? nullptr : entry->second;
            };
            for (const auto& [gridPosition, tile] : tileByGridPosition) {
                const auto [column, row] = gridPosition;
                tile->setNeighbours(neighbourAt(column - 1, row), neighbourAt(column + 1, row),
                                    neighbourAt(column, row - 1), neighbourAt(column, row + 1));
            }
        }

        // This is the single orchestration point for a batch import.
        // NOLINTNEXTLINE(readability-function-size)
        bool createPyramids(const std::vector<ImageFile>& imageFiles, bool explicitTiles,
                            const fs::path& directory, DataNs::TileList& tiles,
                            std::string& error) {
            const fs::path outputDirectory = directory / ".cache" / "pyramids";
            std::error_code errorCode;
            fs::create_directories(outputDirectory, errorCode);
            if (errorCode) {
                error = "cannot create output directory " + outputDirectory.string() +
                        ": " + errorCode.message();
                return false;
            }
            TileByGridPosition tileByGridPosition;
            for (const ImageFile& imageFile : imageFiles)
                if (!appendPyramidTile(imageFile, explicitTiles, outputDirectory, tiles,
                                       tileByGridPosition, error))
                    return false;
            linkTileNeighbours(tileByGridPosition);
            return true;
        }

        Json fingerprintExplicitTiles(const Json& tiles) {
            if (!tiles.is_array() || tiles.empty())
                return Json();
            Json fingerprint = Json::array();
            for (const Json& tile : tiles) {
                if (!tile.is_object() || !tile.contains("path") || !tile["path"].is_string())
                    return Json();
                const fs::path path = tile["path"].get<std::string>();
                if (!fs::is_regular_file(path))
                    return Json();
                fingerprint.push_back({
                    fs::absolute(path).lexically_normal().string(),
                    static_cast<long long>(fs::file_size(path)),
                    static_cast<long long>(fs::last_write_time(path).time_since_epoch().count())
                });
            }
            return fingerprint;
        }

        Json fingerprintDirectory(const fs::path& directory, const std::string& format) {
            const std::regex pattern(format, std::regex::icase);
            std::map<std::string, Json> filesByName;
            for (const auto& entry : fs::directory_iterator(directory)) {
                const std::string fileName = entry.path().filename().string();
                if (!entry.is_regular_file() || !std::regex_match(fileName, pattern))
                    continue;
                filesByName[fileName] = {
                    static_cast<long long>(entry.file_size()),
                    static_cast<long long>(entry.last_write_time().time_since_epoch().count())
                };
            }
            Json fingerprint = Json::array();
            for (const auto& [fileName, sizeAndDate] : filesByName)
                fingerprint.push_back({fileName, sizeAndDate[0], sizeAndDate[1]});
            return fingerprint;
        }
    }

    // Déclare l'identité du step : son nom, ses paramètres (valeurs par défaut = celles de l'entrée) et la donnée
    // qu'il produit. Il n'a pas d'entrée : il lit directement le disque.
    ImportStepClass::ImportStepClass(const ImportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "import";
        stepConfig.parameters = {{"string", "path", initialInput.path},
                                {"string", "format", initialInput.format},
                                {"string", "tiles", initialInput.tiles}};
        stepConfig.outputs = {{"Tiles", "tiles"}};
        configJson = stepConfig;
    }

    // Les images lues sont des données extérieures au pipeline : si l'une change, le checkpoint de ce step n'est plus
    // valable. L'empreinte liste, pour chaque fichier retenu, son nom, sa taille et sa date de modification. Elle est
    // triée par nom, car l'ordre de parcours d'un dossier n'est pas garanti. Dossier ou format invalide : JSON nul
    // (execute() échouera de toute façon).
    Json ImportStepClass::externalInputsFingerprint() const {
        try {
            const fs::path directory = getParameter("path").get<std::string>();
            const Json explicitTiles = Json::parse(getParameter("tiles").get<std::string>());
            if (explicitTiles.is_array() && !explicitTiles.empty())
                return fingerprintExplicitTiles(explicitTiles);
            return fingerprintDirectory(directory, getParameter("format").get<std::string>());
        } catch (const std::exception&) {
            return Json();
        }
    }

    // Étapes : 1. lire les paramètres ; 2. lister et trier les images ; 3. préparer le dossier de sortie ; 4. pour
    // chaque image, créer son TIFF pyramidal et sa fiche ; 5. relier les voisines ; 6. publier les tuiles.
    // The top-level step deliberately keeps validation and publication order visible.
    // NOLINTNEXTLINE(readability-function-size)
    bool ImportStepClass::execute() {
        // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut.
        const ImportInput input{getParameter("path").get<std::string>(),
                                getParameter("format").get<std::string>(),
                                getParameter("tiles").get<std::string>()};
        const fs::path directory = input.path;

        if (!fs::is_directory(directory)) {
            result["error"] = "not a directory: " + directory.string();
            return false;
        }

        std::vector<ImageFile> imageFiles;
        Json explicitTiles;
        try {
            explicitTiles = Json::parse(input.tiles);
        } catch (const Json::exception& exception) {
            result["error"] = std::string("invalid explicit tile list: ") + exception.what();
            return false;
        }
        std::string error;
        if (!collectImageFiles(input, directory, explicitTiles, imageFiles, error)) {
            result["error"] = error;
            return false;
        }

        // Ordre fixe, indépendant de celui du disque : ligne par ligne, puis colonne par colonne. L'identifiant d'une
        // tuile est sa position dans cet ordre.
        std::sort(imageFiles.begin(), imageFiles.end(), [](const ImageFile& first, const ImageFile& second) {
            return first.row != second.row ? first.row < second.row : first.column < second.column;
        });

        DataNs::TileList tiles;
        if (!createPyramids(imageFiles, !explicitTiles.empty(), directory, tiles, error)) {
            result["error"] = error;
            return false;
        }
        output.tiles = std::move(tiles);
        result["tiles"] = output.tiles.size();
        if (dataStore) dataStore->replace("tiles", output.tiles);   // copie légère : les TileClass sont des shared_ptr
        return true;
    }

}
