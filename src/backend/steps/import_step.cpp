// src/backend/steps/import_step.cpp
#include "steps/import_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include "core/io/pyramid_cache.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
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

        // D'où vient la case d'une tuile.
        enum class CellSource : std::uint8_t {
            List,        // la liste `tiles`
            FileName,    // le nom du fichier, lu avec `format`
            NameOrder    // l'ordre des noms, rangés ligne par ligne sur `columns` colonnes
        };

        const char* cellSourceName(CellSource cellSource) {
            switch (cellSource) {
                case CellSource::List: return "list";
                case CellSource::FileName: return "fileName";
                case CellSource::NameOrder: return "nameOrder";
            }
            return "";
        }

        // Une image retenue et sa case dans la grille.
        struct ImageFile {
            fs::path filePath;
            int row = 0;
            int column = 0;
            CellSource cellSource = CellSource::FileName;
        };

        // Paramètres du step, relus dans la configuration.
        struct ImportParameters {
            fs::path directory;
            fs::path outputDirectory;
            std::string format;
            int columns = 0;
            Json tiles;
        };

        bool isTiff(const fs::path& path) {
            std::string extension = IONs::pathToUtf8(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return extension == ".tif" || extension == ".tiff";
        }

        // Sans liste, quand aucun nom ne donne sa case : les TIFF du dossier, triés par nom, remplissent la grille ligne par
        // ligne sur `columns` colonnes (le même ordre que l'interface quand les noms ne donnent pas les cases).
        std::vector<ImageFile> listFilesByNameOrder(const fs::path& directory, const std::string& format, int columns) {
            std::vector<fs::path> filePaths;
            for (const auto& entry : fs::directory_iterator(directory))
                if (entry.is_regular_file() && isTiff(entry.path())) filePaths.push_back(entry.path());
            if (filePaths.empty()) throw std::invalid_argument("no TIFF image (.tif or .tiff) in " + IONs::pathToUtf8(directory));
            if (columns <= 0)
                throw std::invalid_argument("no file name in " + IONs::pathToUtf8(directory) + " gives its grid cell with format '" +
                                            format + "': give the number of columns (parameter 'columns') or the list 'tiles'");
            std::sort(filePaths.begin(), filePaths.end());
            std::vector<ImageFile> imageFiles;
            for (std::size_t index = 0; index < filePaths.size(); ++index) {
                const int rank = static_cast<int>(index);
                imageFiles.push_back({filePaths[index], rank / columns, rank % columns, CellSource::NameOrder});
            }
            return imageFiles;
        }

        // Sans liste : les fichiers du dossier dont le nom correspond à `format` (groupe 1 = colonne, groupe 2 = ligne) ;
        // les autres sont ignorés. Si aucun nom ne correspond, les cases suivent l'ordre des noms (listFilesByNameOrder).
        std::vector<ImageFile> listFilesWithoutList(const fs::path& directory, const std::string& format, int columns) {
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
                imageFiles.push_back({entry.path(), std::stoi(nameMatch[2].str()), std::stoi(nameMatch[1].str()), CellSource::FileName});
            }
            return imageFiles.empty() ? listFilesByNameOrder(directory, format, columns) : imageFiles;
        }

        bool hasIntegerField(const Json& entry, const char* name) {
            return entry.contains(name) && entry.at(name).is_number_integer();
        }

        // Mode liste : une entrée { "file", "row", "column" }. Un fichier relatif est cherché dans `directory`.
        // L'identifiant n'est pas dans la liste : import le calcule à partir de la case (voir sortAndCheckGrid).
        ImageFile readListedTile(const fs::path& directory, const Json& entry, std::size_t entryIndex) {
            const std::string where = "tiles[" + std::to_string(entryIndex) + "]";
            if (!entry.is_object() || !entry.contains("file") || !entry.at("file").is_string() || !hasIntegerField(entry, "row") ||
                !hasIntegerField(entry, "column"))
                throw std::invalid_argument(where + " must be an object { file, row, column } (file: string, row and column: integers)");
            const int row = entry.at("row").get<int>();
            const int column = entry.at("column").get<int>();
            if (row < 0 || column < 0) throw std::invalid_argument(where + ": row and column must be non-negative");
            const fs::path listedPath = IONs::pathFromUtf8(entry.at("file").get<std::string>());
            const fs::path filePath = listedPath.is_absolute() ? listedPath : directory / listedPath;
            if (!fs::is_regular_file(filePath)) throw std::invalid_argument(where + ": missing file " + IONs::pathToUtf8(filePath));
            return {filePath, row, column, CellSource::List};
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

        // Images à importer : la liste `tiles` si elle n'est pas vide, sinon les fichiers du dossier (listFilesWithoutList).
        std::vector<ImageFile> listImageFiles(const ImportParameters& parameters) {
            if (!parameters.tiles.is_array()) throw std::invalid_argument("parameter 'tiles' must be an array");
            if (parameters.columns < 0) throw std::invalid_argument("parameter 'columns' must be non-negative");
            return parameters.tiles.empty() ? listFilesWithoutList(parameters.directory, parameters.format, parameters.columns)
                                            : listFilesFromList(parameters.directory, parameters.tiles);
        }

        // Ordre fixe, indépendant de celui du disque ou de la liste : ligne par ligne, puis colonne par colonne.
        // L'identifiant d'une tuile est sa position dans cet ordre (voir execute). Deux tuiles ne peuvent pas occuper
        // la même case.
        void sortAndCheckGrid(std::vector<ImageFile>& imageFiles) {
            std::sort(imageFiles.begin(), imageFiles.end(), [](const ImageFile& first, const ImageFile& second) {
                return first.row != second.row ? first.row < second.row : first.column < second.column;
            });
            for (std::size_t tileIndex = 1; tileIndex < imageFiles.size(); ++tileIndex) {
                const ImageFile& previous = imageFiles[tileIndex - 1];
                const ImageFile& imageFile = imageFiles[tileIndex];
                if (previous.row == imageFile.row && previous.column == imageFile.column)
                    throw std::invalid_argument("two tiles share the grid cell (row " + std::to_string(imageFile.row) + ", column " +
                                                std::to_string(imageFile.column) + "): " + IONs::pathToUtf8(previous.filePath) +
                                                " and " + IONs::pathToUtf8(imageFile.filePath));
            }
        }

        // Dossier des TIFF pyramidaux : créé s'il n'existe pas, puis vérifié (le deuxième appel renvoie false sans
        // erreur si le dossier existe déjà ; une erreur est signalée par errorCode).
        fs::path prepareOutputDirectory(const fs::path& directory) {
            const fs::path outputDirectory = IONs::pyramidCacheDirectory(directory);
            fs::create_directories(outputDirectory);
            std::error_code errorCode;
            if (!fs::create_directories(outputDirectory, errorCode) && errorCode)
                throw std::runtime_error("cannot create output directory " + IONs::pathToUtf8(outputDirectory) + ": " + errorCode.message());
            return outputDirectory;
        }

        // Un TIFF par image, dans le cache partagé avec l'interface (core/io/pyramid_cache.h) :
        // <nom de l'image>_<empreinte du chemin complet>_pyramid.tif. L'empreinte rend le nom unique, même pour deux
        // images de même nom (a.tif et a.tiff, ou deux dossiers de la liste `tiles`), et indépendant de la case : une
        // grille réorganisée réutilise ses pyramides. Le nom de l'image le garde lisible.
        // Pyramide valide (même source, même taille, même date) : réutilisée sans lire l'image. Sinon, une seule image
        // en mémoire à la fois : lue, convertie, puis libérée. Seule la fiche reste.
        std::shared_ptr<DataNs::TileClass> createTile(const ImageFile& imageFile, int tileId, const fs::path& outputDirectory) {
            IONs::PyramidCacheEntry pyramid;
            std::string errorMessage;
            if (!IONs::preparePyramidCache(imageFile.filePath, outputDirectory, pyramid, errorMessage))
                throw std::runtime_error("cannot create the pyramid of " + IONs::pathToUtf8(imageFile.filePath) + ": " + errorMessage);

            auto tile = std::make_shared<DataNs::TileClass>(tileId, imageFile.filePath, pyramid.pyramidPath, pyramid.width,
                                                            pyramid.height, pyramid.type);
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
                                 {"int", "columns", initialInput.columns}, {"json", "tiles", initialInput.tiles},
                                 {"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.outputs = {{"Tiles", "tiles"}};
        configJson = stepConfig;
    }

    // Les images lues sont des données extérieures au pipeline : si l'une change, le checkpoint de ce step n'est plus
    // valable. L'empreinte liste, pour chaque fichier retenu, son nom (son chemin complet en mode liste), sa taille et sa
    // date de modification. Elle est triée par nom, car l'ordre de parcours d'un dossier n'est pas garanti. Paramètres
    // invalides ou aucune image : JSON nul (execute() échouera de toute façon).
    Json ImportStepClass::externalInputsFingerprint() const {
        try {
            std::string configuredOutputDirectory;
            try {
                configuredOutputDirectory = getParameter("outputDirectory").get<std::string>();
            } catch (const std::out_of_range&) {}
            const ImportParameters parameters{
                IONs::pathFromUtf8(getParameter("path").get<std::string>()),
                IONs::pathFromUtf8(configuredOutputDirectory),
                getParameter("format").get<std::string>(),
                getParameter("columns").get<int>(), getParameter("tiles")};
            std::map<std::string, Json> filesByName;
            for (const ImageFile& imageFile : listImageFiles(parameters)) {
                const bool listed = imageFile.cellSource == CellSource::List;
                const std::string fileName = IONs::pathToUtf8(listed ? imageFile.filePath : imageFile.filePath.filename());
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
            std::string configuredOutputDirectory;
            try {
                configuredOutputDirectory = getParameter("outputDirectory").get<std::string>();
            } catch (const std::out_of_range&) {}
            const ImportParameters parameters{
                IONs::pathFromUtf8(getParameter("path").get<std::string>()),
                IONs::pathFromUtf8(configuredOutputDirectory),
                getParameter("format").get<std::string>(),
                getParameter("columns").get<int>(), getParameter("tiles")};
            if (!fs::is_directory(parameters.directory))
                throw std::invalid_argument("not a directory: " + IONs::pathToUtf8(parameters.directory));
            std::vector<ImageFile> imageFiles = listImageFiles(parameters);
            sortAndCheckGrid(imageFiles);
            // Comment les cases ont été trouvées : liste, noms des fichiers, ou ordre des noms.
            result["placement"] = cellSourceName(imageFiles.front().cellSource);
            const fs::path outputDirectory = parameters.outputDirectory.empty()
                ? prepareOutputDirectory(parameters.directory)
                : parameters.outputDirectory / ".cache" / "pyramids";
            fs::create_directories(outputDirectory);
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
