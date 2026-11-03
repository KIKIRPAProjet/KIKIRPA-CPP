// src/backend/steps/import_step.cpp
#include "steps/import_step.h"

#include "core/config/pipeline_config.h"
#include "core/io/pyramidal_tiff_writer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace StepsNs {

    namespace fs = std::filesystem;

    // Déclare l'identité du step : son nom, ses deux paramètres (valeurs par défaut = celles de l'entrée) et la donnée
    // qu'il produit. Il n'a pas d'entrée : il lit directement le disque.
    ImportStepClass::ImportStepClass(const ImportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "import";
        stepConfig.parameters = {{"string", "path", initialInput.path}, {"string", "format", initialInput.format}};
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
            const std::regex filenamePattern(getParameter("format").get<std::string>(), std::regex::icase);
            std::map<std::string, Json> filesByName;
            for (const auto& entry : fs::directory_iterator(directory)) {
                const std::string fileName = entry.path().filename().string();
                if (!entry.is_regular_file() || !std::regex_match(fileName, filenamePattern)) continue;
                filesByName[fileName] = {static_cast<long long>(entry.file_size()),
                                         static_cast<long long>(entry.last_write_time().time_since_epoch().count())};
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
        // Paramètres relus dans la configuration : un JSON chargé après la construction remplace les valeurs par défaut.
        const ImportInput input{getParameter("path").get<std::string>(), getParameter("format").get<std::string>()};
        const fs::path directory = input.path;

        if (!fs::is_directory(directory)) {
            result["error"] = "not a directory: " + directory.string();
            return false;
        }

        std::regex filenamePattern;
        try {
            filenamePattern = std::regex(input.format, std::regex::icase);   // .tif et .TIF
        } catch (const std::regex_error& exception) {
            result["error"] = "invalid format '" + input.format + "': " + exception.what();
            return false;
        }
        if (filenamePattern.mark_count() < 2) {
            result["error"] = "format '" + input.format + "' needs 2 groups: (column) and (row)";
            return false;
        }

        // Fichiers dont le nom correspond au format ; groupe 1 = colonne, groupe 2 = ligne.
        struct ImageFile { fs::path filePath; int column, row; };
        std::vector<ImageFile> imageFiles;
        for (const auto& entry : fs::directory_iterator(directory)) {
            const std::string fileName = entry.path().filename().string();
            std::smatch nameMatch;
            if (!entry.is_regular_file() || !std::regex_match(fileName, nameMatch, filenamePattern)) continue;
            imageFiles.push_back({entry.path(), std::stoi(nameMatch[1].str()), std::stoi(nameMatch[2].str())});
        }

        if (imageFiles.empty()) {
            result["error"] = "no file matching '" + input.format + "' in " + directory.string();
            return false;
        }

        // Ordre fixe, indépendant de celui du disque : ligne par ligne, puis colonne par colonne. L'identifiant d'une
        // tuile est sa position dans cet ordre.
        std::sort(imageFiles.begin(), imageFiles.end(), [](const ImageFile& first, const ImageFile& second) {
            return first.row != second.row ? first.row < second.row : first.column < second.column;
        });

        // Dossier des TIFF pyramidaux : créé s'il n'existe pas, puis vérifié (le deuxième appel renvoie false sans
        // erreur si le dossier existe déjà ; une erreur est signalée par errorCode).
        const fs::path outputDirectory = directory / ".cache" / "pyramids";
        fs::create_directories(outputDirectory);

        std::error_code errorCode;
        if (!fs::create_directories(outputDirectory, errorCode)
            && errorCode) {
            result["error"] =
                "cannot create output directory " + outputDirectory.string()
                + ": " + errorCode.message();
            return false;
        }

        // Une seule image en mémoire à la fois : lue, convertie en TIFF pyramidal, puis libérée. Seule la fiche reste.
        DataNs::TileList tiles;
        std::map<std::pair<int, int>, DataNs::TileClass*> tileByGridPosition;   // (colonne, ligne) -> tuile
        for (const auto& imageFile : imageFiles) {
            const cv::Mat image = cv::imread(imageFile.filePath.string(), cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
            if (image.empty()) {
                result["error"] = "cannot read " + imageFile.filePath.string();
                return false;
            }

            // Un TIFF par image, nommé d'après l'image d'origine : <nom>_pyramid.tif.
            const fs::path pyramidPath = outputDirectory / (imageFile.filePath.stem().string() + "_pyramid.tif");

            std::string errorMessage;
            if (!IONs::createPyramidalTiledTiff(image, pyramidPath, errorMessage)) {
                result["error"] =
                    "cannot create " + pyramidPath.string() + ": " + errorMessage;
                return false;
            }

            tiles.push_back(std::make_shared<DataNs::TileClass>(
                static_cast<int>(tiles.size()),
                imageFile.filePath,
                pyramidPath,
                image.cols,
                image.rows,
                image.type()
            ));
            tileByGridPosition[{imageFile.column, imageFile.row}] = tiles.back().get();
        }

        // Voisines : la tuile voisine existe si la grille contient la colonne ou la ligne adjacente, sinon nullptr
        // (grille incomplète ou bord de la mosaïque).
        auto neighbourAt = [&](int column, int row) -> DataNs::TileClass* {
            auto entry = tileByGridPosition.find({column, row});
            return entry == tileByGridPosition.end() ? nullptr : entry->second;
        };
        for (const auto& [gridPosition, tile] : tileByGridPosition) {
            const auto [column, row] = gridPosition;
            tile->setNeighbours(neighbourAt(column - 1, row), neighbourAt(column + 1, row), neighbourAt(column, row - 1), neighbourAt(column, row + 1));
        }

        output.tiles = std::move(tiles);
        result["tiles"] = output.tiles.size();
        if (dataStore) dataStore->replace("tiles", output.tiles);   // copie légère : les TileClass sont des shared_ptr
        return true;
    }

}
