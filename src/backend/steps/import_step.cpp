// src/backend/steps/import_step.cpp
#include "steps/import_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace fs = std::filesystem;

    namespace {

        // Une image retenue et sa case dans la grille.
        struct ImageFile {
            fs::path filePath;
            int row = 0;
            int column = 0;
            std::optional<int> listedId;   // id écrit dans la liste `tiles` ; absent en mode format
        };

        // Mode format : fichiers du dossier dont le nom correspond à `format` ; groupe 1 = colonne, groupe 2 = ligne.
        std::vector<ImageFile> listFilesByFormat(const fs::path& directory, const std::string& format) {
            std::regex filenamePattern;
            try {
                filenamePattern = std::regex(format, std::regex::icase);   // .tif et .TIF
            } catch (const std::regex_error& exception) {
                throw std::invalid_argument("invalid format '" + format + "': " + exception.what());
            }
            if (filenamePattern.mark_count() < 2)
                throw std::invalid_argument("format '" + format + "' needs 2 groups: (column) and (row)");
            std::vector<ImageFile> imageFiles;
            for (const auto& entry : fs::directory_iterator(directory)) {
                const std::string fileName = IONs::pathToUtf8(entry.path().filename());
                std::smatch nameMatch;
                if (!entry.is_regular_file() || !std::regex_match(fileName, nameMatch, filenamePattern)) continue;
                imageFiles.push_back({entry.path(), std::stoi(nameMatch[2].str()), std::stoi(nameMatch[1].str()), std::nullopt});
            }
            if (imageFiles.empty())
                throw std::invalid_argument("no file matching '" + format + "' in " + IONs::pathToUtf8(directory));
            return imageFiles;
        }

        bool hasIntegerField(const Json& entry, const char* name) {
            return entry.contains(name) && entry.at(name).is_number_integer();
        }

        // Mode liste : une entrée { "id", "file", "row", "column" }. Un fichier relatif est cherché dans `directory`.
        ImageFile readListedTile(const fs::path& directory, const Json& entry, std::size_t entryIndex) {
            const std::string where = "tiles[" + std::to_string(entryIndex) + "]";
            if (!entry.is_object() || !entry.contains("file") || !entry.at("file").is_string() || !hasIntegerField(entry, "id") ||
                !hasIntegerField(entry, "row") || !hasIntegerField(entry, "column"))
                throw std::invalid_argument(where + " must be an object { id, file, row, column } (file: string, others: integers)");
            const int row = entry.at("row").get<int>();
            const int column = entry.at("column").get<int>();
            if (row < 0 || column < 0) throw std::invalid_argument(where + ": row and column must be non-negative");
            const fs::path listedPath = IONs::pathFromUtf8(entry.at("file").get<std::string>());
            const fs::path filePath = listedPath.is_absolute() ? listedPath : directory / listedPath;
            if (!fs::is_regular_file(filePath)) throw std::invalid_argument(where + ": missing file " + IONs::pathToUtf8(filePath));
            return {filePath, row, column, entry.at("id").get<int>()};
        }

        std::vector<ImageFile> listFilesFromList(const fs::path& directory, const Json& tilesJson) {
            std::vector<ImageFile> imageFiles;
            std::set<fs::path> filePaths;
            for (std::size_t entryIndex = 0; entryIndex < tilesJson.size(); ++entryIndex) {
                imageFiles.push_back(readListedTile(directory, tilesJson.at(entryIndex), entryIndex));
                if (!filePaths.insert(fs::absolute(imageFiles.back().filePath).lexically_normal()).second)
                    throw std::invalid_argument("tiles: file listed twice: " + IONs::pathToUtf8(imageFiles.back().filePath));
            }
            return imageFiles;
        }

        // Images à importer : la liste `tiles` si elle n'est pas vide, sinon les fichiers qui correspondent à `format`.
        std::vector<ImageFile> listImageFiles(const fs::path& directory, const std::string& format, const Json& tilesJson) {
            if (!tilesJson.is_array()) throw std::invalid_argument("parameter 'tiles' must be an array");
            return tilesJson.empty() ? listFilesByFormat(directory, format) : listFilesFromList(directory, tilesJson);
        }

        // Ordre fixe, indépendant de celui du disque ou de la liste : ligne par ligne, puis colonne par colonne.
        // L'identifiant d'une tuile est sa position dans cet ordre. Deux tuiles ne peuvent pas occuper la même case, et
        // un id donné par la liste doit être ce rang.
        void sortAndCheckGrid(std::vector<ImageFile>& imageFiles) {
            std::sort(imageFiles.begin(), imageFiles.end(), [](const ImageFile& first, const ImageFile& second) {
                return first.row != second.row ? first.row < second.row : first.column < second.column;
            });
            for (std::size_t tileIndex = 0; tileIndex < imageFiles.size(); ++tileIndex) {
                const ImageFile& imageFile = imageFiles[tileIndex];
                const std::string cell = "(row " + std::to_string(imageFile.row) + ", column " + std::to_string(imageFile.column) + ")";
                if (tileIndex > 0 && imageFiles[tileIndex - 1].row == imageFile.row && imageFiles[tileIndex - 1].column == imageFile.column)
                    throw std::invalid_argument("two tiles share the grid cell " + cell + ": " +
                                                IONs::pathToUtf8(imageFiles[tileIndex - 1].filePath) + " and " + IONs::pathToUtf8(imageFile.filePath));
                if (imageFile.listedId && *imageFile.listedId != static_cast<int>(tileIndex))
                    throw std::invalid_argument("tiles: the tile in cell " + cell + " must have id " + std::to_string(tileIndex) +
                                                " (ids follow the order row by row, then column by column)");
            }
        }

        // Dossier des TIFF pyramidaux : créé s'il n'existe pas, puis vérifié (le deuxième appel renvoie false sans
        // erreur si le dossier existe déjà ; une erreur est signalée par errorCode).
        fs::path prepareOutputDirectory(const fs::path& directory) {
            const fs::path outputDirectory = directory / ".cache" / "pyramids";
            fs::create_directories(outputDirectory);
            std::error_code errorCode;
            if (!fs::create_directories(outputDirectory, errorCode) && errorCode)
                throw std::runtime_error("cannot create output directory " + IONs::pathToUtf8(outputDirectory) + ": " + errorCode.message());
            return outputDirectory;
        }

        // Une seule image en mémoire à la fois : lue, convertie en TIFF pyramidal, puis libérée. Seule la fiche reste.
        std::shared_ptr<DataNs::TileClass> createTile(const ImageFile& imageFile, int tileId, const fs::path& outputDirectory) {
            const cv::Mat image = IONs::readImage(imageFile.filePath, cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
            if (image.empty()) throw std::runtime_error("cannot read " + IONs::pathToUtf8(imageFile.filePath));

            // Un TIFF par image, nommé d'après l'image d'origine : <nom>_pyramid.tif.
            fs::path pyramidFileName = imageFile.filePath.stem();
            pyramidFileName += "_pyramid.tif";
            const fs::path pyramidPath = outputDirectory / pyramidFileName;
            std::string errorMessage;
            if (!IONs::createPyramidalTiledTiff(image, pyramidPath, errorMessage))
                throw std::runtime_error("cannot create " + IONs::pathToUtf8(pyramidPath) + ": " + errorMessage);

            auto tile = std::make_shared<DataNs::TileClass>(tileId, imageFile.filePath, pyramidPath, image.cols, image.rows, image.type());
            tile->setGridPosition(imageFile.row, imageFile.column);
            return tile;
        }

        // Voisines : la tuile voisine existe si la grille contient la colonne ou la ligne adjacente, sinon nullptr
        // (grille incomplète ou bord de la mosaïque).
        void linkNeighbours(const DataNs::TileList& tiles) {
            std::map<std::pair<int, int>, DataNs::TileClass*> tileByGridPosition;   // (ligne, colonne) -> tuile
            for (const auto& tile : tiles) tileByGridPosition[{tile->getGridRow(), tile->getGridColumn()}] = tile.get();
            const auto neighbourAt = [&](int row, int column) -> DataNs::TileClass* {
                const auto entry = tileByGridPosition.find({row, column});
                return entry == tileByGridPosition.end() ? nullptr : entry->second;
            };
            for (const auto& tile : tiles) {
                const int row = tile->getGridRow();
                const int column = tile->getGridColumn();
                tile->setNeighbours(neighbourAt(row, column - 1), neighbourAt(row, column + 1), neighbourAt(row - 1, column), neighbourAt(row + 1, column));
            }
        }

    }

    // Déclare l'identité du step : son nom, ses paramètres (valeurs par défaut = celles de l'entrée) et la donnée
    // qu'il produit. Il n'a pas d'entrée : il lit directement le disque.
    ImportStepClass::ImportStepClass(const ImportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "import";
        stepConfig.parameters = {{"string", "path", initialInput.path}, {"string", "format", initialInput.format},
                                 {"json", "tiles", initialInput.tiles}};
        stepConfig.outputs = {{"Tiles", "tiles"}};
        configJson = stepConfig;
    }

    // Les images lues sont des données extérieures au pipeline : si l'une change, le checkpoint de ce step n'est plus
    // valable. L'empreinte liste, pour chaque fichier retenu, son nom (son chemin complet en mode liste), sa taille et sa
    // date de modification. Elle est triée par nom, car l'ordre de parcours d'un dossier n'est pas garanti. Dossier,
    // format ou liste invalide : JSON nul (execute() échouera de toute façon).
    Json ImportStepClass::externalInputsFingerprint() const {
        try {
            const fs::path directory = IONs::pathFromUtf8(getParameter("path").get<std::string>());
            std::map<std::string, Json> filesByName;
            for (const ImageFile& imageFile : listImageFiles(directory, getParameter("format").get<std::string>(), getParameter("tiles"))) {
                const std::string fileName = IONs::pathToUtf8(imageFile.listedId ? imageFile.filePath : imageFile.filePath.filename());
                filesByName[fileName] = {static_cast<long long>(fs::file_size(imageFile.filePath)),
                                         static_cast<long long>(fs::last_write_time(imageFile.filePath).time_since_epoch().count())};
            }
            Json fingerprint = Json::array();
            for (const auto& [fileName, sizeAndDate] : filesByName) fingerprint.push_back({fileName, sizeAndDate[0], sizeAndDate[1]});
            return fingerprint;
        } catch (const std::exception&) {
            return Json();
        }
    }

    // Étapes : 1. lire les paramètres ; 2. lister et trier les images ; 3. préparer le dossier de sortie ; 4. pour
    // chaque image, créer son TIFF pyramidal et sa fiche ; 5. relier les voisines ; 6. publier les tuiles.
    bool ImportStepClass::execute() {
        try {
            // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut.
            const fs::path directory = IONs::pathFromUtf8(getParameter("path").get<std::string>());
            if (!fs::is_directory(directory)) throw std::invalid_argument("not a directory: " + IONs::pathToUtf8(directory));
            std::vector<ImageFile> imageFiles = listImageFiles(directory, getParameter("format").get<std::string>(), getParameter("tiles"));
            sortAndCheckGrid(imageFiles);
            const fs::path outputDirectory = prepareOutputDirectory(directory);
            DataNs::TileList tiles;
            for (const ImageFile& imageFile : imageFiles) tiles.push_back(createTile(imageFile, static_cast<int>(tiles.size()), outputDirectory));
            linkNeighbours(tiles);
            output.tiles = std::move(tiles);
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
        result["tiles"] = output.tiles.size();
        if (dataStore) dataStore->replace("tiles", output.tiles);   // copie légère : les TileClass sont des shared_ptr
        return true;
    }

}
