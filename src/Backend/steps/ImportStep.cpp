#include "steps/ImportStep.h"
#include "Backend/core/PyramidalTiffWriter.h"
#include "core/Config.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <filesystem>
#include <map>
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
    struct Fichier { fs::path path; int col, row; };
    std::vector<Fichier> fichiers;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        std::smatch m;
        if (!entry.is_regular_file() || !std::regex_match(name, m, format)) continue;
        fichiers.push_back({entry.path(), std::stoi(m[1].str()), std::stoi(m[2].str())});
    }

    if (fichiers.empty()) {
        outputData["error"] = "no file matching '" + input.format + "' in " + dir.string();
        return false;
    }

    // Ligne par ligne, puis colonne par colonne
    std::sort(fichiers.begin(), fichiers.end(), [](const Fichier& a, const Fichier& b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });

    // Dossier de sortie pour les TIFF tuilés pyramides
    const fs::path outputDir = dir / ".cache" / "pyramids";
    fs::create_directories(outputDir);

    // Vérification de la création du dossier de sortie
    std::error_code errorCode;
    if (!fs::create_directories(outputDir, errorCode)
        && errorCode) {
        outputData["error"] =
            "cannot create output directory " + outputDir.string()
            + ": " + errorCode.message();
        return false;
    }

    // Une seule image en RAM à la fois : lue une fois, puis libérée. Seule la fiche est gardée.
    Tiles tiles;
    std::map<std::pair<int, int>, Tuile*> grille;   // (colonne, ligne) -> tuile
    for (const auto& f : fichiers) {
        const cv::Mat image = cv::imread(f.path.string(), cv::IMREAD_UNCHANGED);   // garde le 16 bits
        if (image.empty()) {
            outputData["error"] = "cannot read " + f.path.string();
            return false;
        }

        // Chemin du TIFF tuilé pyramidal à créer dans le dossier de sortie
        const fs::path pyramidPath = outputDir / (f.path.stem().string() + "_pyramid.tif");

        // Appel à la fonction de création du TIFF tuilé pyramidal (qui crée le tiff et l'enregistre)
        std::string error;
        if (!createPyramidalTiledTiff(image, pyramidPath, error)) {
            outputData["error"] =
                "cannot create " + pyramidPath.string() + ": " + error;
            return false;
        }

        tiles.push_back(std::make_shared<Tuile>(
            static_cast<int>(tiles.size()),
            f.path,
            pyramidPath,
            image.cols,
            image.rows,
            image.type()
        ));
        grille[{f.col, f.row}] = tiles.back().get();
    }

    auto voisin = [&](int col, int row) -> Tuile* {
        auto it = grille.find({col, row});
        return it == grille.end() ? nullptr : it->second;
    };
    for (const auto& [pos, t] : grille) {
        const auto [col, row] = pos;
        t->setVoisins(voisin(col - 1, row), voisin(col + 1, row), voisin(col, row - 1), voisin(col, row + 1));
    }

    output.tiles = std::move(tiles);
    outputData["tiles"] = output.tiles.size();
    if (data) data->set("tiles", output.tiles);   // pour les steps suivants (copie légère : shared_ptr)
    return true;
}

} // namespace stitching::steps
