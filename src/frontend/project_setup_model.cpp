#include "project_setup_model.h"

#include "core/pipeline/pipeline_builder.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace FrontendNs {

namespace fs = std::filesystem;

namespace {

bool isTiff(const fs::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return extension == ".tif" || extension == ".tiff";
}

std::vector<fs::path> collectTiffPaths(const fs::path &directory) {
  std::vector<fs::path> paths;
  for (const fs::directory_entry &entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && isTiff(entry.path()))
      paths.push_back(fs::absolute(entry.path()).lexically_normal());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

std::optional<std::regex> compilePattern(const std::string &format) {
  try {
    std::regex pattern(format, std::regex::icase);
    if (pattern.mark_count() >= 2)
      return pattern;
  } catch (const std::regex_error &) {
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ProjectTile>
tileFromFilename(const fs::path &path, const std::regex &pattern,
                 const ProjectSetup &setup,
                 std::set<std::pair<int, int>> &occupiedCells) {
  std::smatch match;
  const std::string fileName = path.filename().string();
  if (!std::regex_match(fileName, match, pattern))
    return std::nullopt;
  try {
    const int column = std::stoi(match[1].str());
    const int row = std::stoi(match[2].str());
    if (row < 0 || row >= setup.gridRows || column < 0 ||
        column >= setup.gridColumns ||
        !occupiedCells.insert({row, column}).second)
      return std::nullopt;
    return ProjectTile{path, row, column};
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::vector<ProjectTile>
tilesFromCoordinates(const std::vector<fs::path> &paths,
                     const ProjectSetup &setup, const std::regex &pattern) {
  std::set<std::pair<int, int>> occupiedCells;
  std::vector<ProjectTile> tiles;
  for (const fs::path &path : paths) {
    std::optional<ProjectTile> tile =
        tileFromFilename(path, pattern, setup, occupiedCells);
    if (!tile)
      return {};
    tiles.push_back(std::move(*tile));
  }
  std::sort(tiles.begin(), tiles.end(),
            [](const ProjectTile &first, const ProjectTile &second) {
              return first.row != second.row ? first.row < second.row
                                             : first.column < second.column;
            });
  return tiles;
}

std::vector<ProjectTile> tilesInReadingOrder(const std::vector<fs::path> &paths,
                                             int columns) {
  std::vector<ProjectTile> tiles;
  tiles.reserve(paths.size());
  for (std::size_t index = 0; index < paths.size(); ++index)
    tiles.push_back({paths[index], static_cast<int>(index) / columns,
                     static_cast<int>(index) % columns});
  return tiles;
}

void replaceParameter(Json &stepJson, const std::string &name, Json value) {
  for (Json &parameter : stepJson.at("parameters")) {
    if (parameter.value("name", "") == name) {
      parameter["value"] = std::move(value);
      return;
    }
  }
  throw std::logic_error("missing backend parameter '" + name + "'");
}

std::string safeFileStem(const std::string &projectName) {
  std::string result;
  result.reserve(projectName.size());
  for (const unsigned char character : projectName) {
    if (std::isalnum(character) || character == '-' || character == '_')
      result.push_back(static_cast<char>(character));
    else if (!result.empty() && result.back() != '-')
      result.push_back('-');
  }
  while (!result.empty() && result.back() == '-')
    result.pop_back();
  return result.empty() ? "pipeline" : result;
}

void validateSettings(const ProjectSetup &setup,
                      std::vector<std::string> &errors) {
  if (setup.projectName.empty())
    errors.emplace_back("Le nom du projet est obligatoire.");
  if (!fs::is_directory(setup.inputDirectory))
    errors.emplace_back("Le dossier d'entrée est absent ou invalide.");
  if (setup.outputDirectory.empty())
    errors.emplace_back("Le dossier de sortie est obligatoire.");
  if (setup.gridRows < 1 || setup.gridColumns < 1 ||
      setup.gridRows > MAX_GRID_DIMENSION ||
      setup.gridColumns > MAX_GRID_DIMENSION)
    errors.emplace_back("Les dimensions de la grille sont invalides.");
  if (!std::isfinite(setup.overlap) || setup.overlap <= 0.0 ||
      setup.overlap >= 1.0)
    errors.emplace_back(
        "Le recouvrement doit être strictement compris entre 0 et 100 %.");
  if (setup.pyramidLevel < 0 || setup.pyramidLevel > MAX_PYRAMID_LEVEL)
    errors.emplace_back(
        "Le niveau de pyramide doit être compris entre 0 et 5.");
}

std::optional<std::regex> validatedPattern(const ProjectSetup &setup,
                                           std::vector<std::string> &errors) {
  if (setup.filenameFormat.empty())
    return std::nullopt;
  try {
    std::regex pattern(setup.filenameFormat, std::regex::icase);
    if (pattern.mark_count() < 2) {
      errors.emplace_back("Le format des noms doit contenir deux groupes : "
                          "colonne puis ligne.");
      return std::nullopt;
    }
    return pattern;
  } catch (const std::regex_error &) {
    errors.emplace_back("Le format des noms de fichiers n'est pas une "
                        "expression régulière valide.");
    return std::nullopt;
  }
}

void validateTileCoordinates(const ProjectTile &tile, const std::regex &pattern,
                             std::vector<std::string> &errors) {
  std::smatch match;
  const std::string fileName = tile.path.filename().string();
  if (!std::regex_match(fileName, match, pattern)) {
    errors.emplace_back("Nom incompatible avec le format : " + fileName);
    return;
  }
  try {
    const int parsedColumn = std::stoi(match[1].str());
    const int parsedRow = std::stoi(match[2].str());
    if (parsedColumn != tile.column || parsedRow != tile.row)
      errors.emplace_back("La position de " + fileName +
                          " ne correspond pas à ses coordonnées dans le nom.");
  } catch (const std::exception &) {
    errors.emplace_back("Coordonnées invalides dans le nom : " + fileName);
  }
}

struct TileValidationContext {
  const ProjectSetup &setup;
  const std::optional<std::regex> &pattern;
  std::set<fs::path> uniquePaths;
  std::set<std::pair<int, int>> uniqueCells;
  std::vector<std::string> &errors;
};

void validateTile(const ProjectTile &tile, TileValidationContext &context) {
  const fs::path absolutePath = fs::absolute(tile.path).lexically_normal();
  if (!context.uniquePaths.insert(absolutePath).second)
    context.errors.emplace_back("Tuile dupliquée : " +
                                absolutePath.filename().string());
  if (!fs::is_regular_file(absolutePath))
    context.errors.emplace_back("Fichier absent : " + absolutePath.string());
  if (!context.uniqueCells.insert({tile.row, tile.column}).second)
    context.errors.emplace_back("Plusieurs tuiles occupent la case (" +
                                std::to_string(tile.row + 1) + ", " +
                                std::to_string(tile.column + 1) + ").");
  if (tile.row < 0 || tile.row >= context.setup.gridRows || tile.column < 0 ||
      tile.column >= context.setup.gridColumns)
    context.errors.emplace_back("Tuile hors de la grille : " +
                                absolutePath.filename().string());
  if (context.pattern)
    validateTileCoordinates(tile, *context.pattern, context.errors);
}

Json createGridJson(const ProjectSetup &setup) {
  Json tiles = Json::array();
  for (const ProjectTile &tile : setup.tiles)
    tiles.push_back(
        {{"row", tile.row},
         {"column", tile.column},
         {"path", fs::absolute(tile.path).lexically_normal().string()}});
  return {{"rows", setup.gridRows},
          {"columns", setup.gridColumns},
          {"tiles", std::move(tiles)}};
}

Json createProjectJson(const ProjectSetup &setup) {
  return {{"formatVersion", "1.0"},
          {"name", setup.projectName},
          {"inputDirectory",
           fs::absolute(setup.inputDirectory).lexically_normal().string()},
          {"outputDirectory",
           fs::absolute(setup.outputDirectory).lexically_normal().string()},
          {"grid", createGridJson(setup)}};
}

Json createRunJson(const ProjectSetup &setup) {
  const fs::path output =
      fs::absolute(setup.outputDirectory).lexically_normal();
  return {{"status", "idle"},
          {"completedSteps", Json::array()},
          {"skipCompletedSteps", true},
          {"startFromStep", ""},
          {"checkpointDirectory", (output / "checkpoints").string()}};
}

} // namespace

void ProjectSetupModelClass::setSetup(ProjectSetup newSetup) {
  setup = std::move(newSetup);
}

void ProjectSetupModelClass::importTiles() {
  if (!fs::is_directory(setup.inputDirectory))
    throw std::invalid_argument("Le dossier d'entrée n'existe pas.");

  const std::vector<fs::path> paths = collectTiffPaths(setup.inputDirectory);
  const std::optional<std::regex> pattern =
      compilePattern(setup.filenameFormat);
  setup.tiles = pattern ? tilesFromCoordinates(paths, setup, *pattern)
                        : std::vector<ProjectTile>{};
  if (setup.tiles.empty() && !paths.empty())
    setup.tiles = tilesInReadingOrder(paths, setup.gridColumns);
}

void ProjectSetupModelClass::resizeGrid(int rows, int columns) {
  if (rows < 1 || columns < 1 || rows > MAX_GRID_DIMENSION ||
      columns > MAX_GRID_DIMENSION)
    throw std::invalid_argument(
        "La grille doit contenir entre 1 et 20 lignes et colonnes.");
  setup.gridRows = rows;
  setup.gridColumns = columns;
  for (std::size_t index = 0; index < setup.tiles.size(); ++index) {
    setup.tiles[index].row = static_cast<int>(index) / columns;
    setup.tiles[index].column = static_cast<int>(index) % columns;
  }
}

void ProjectSetupModelClass::swapTiles(std::size_t firstIndex,
                                       std::size_t secondIndex) {
  if (firstIndex >= setup.tiles.size() || secondIndex >= setup.tiles.size())
    return;
  std::swap(setup.tiles[firstIndex].path, setup.tiles[secondIndex].path);
}

std::vector<std::string> ProjectSetupModelClass::validate() const {
  std::vector<std::string> errors;
  validateSettings(setup, errors);
  const std::size_t cellCount =
      static_cast<std::size_t>(std::max(0, setup.gridRows * setup.gridColumns));
  if (setup.tiles.size() != cellCount)
    errors.emplace_back(
        "La grille doit contenir exactement une tuile par case (" +
        std::to_string(cellCount) + " attendues, " +
        std::to_string(setup.tiles.size()) + " trouvées).");

  const std::optional<std::regex> pattern = validatedPattern(setup, errors);
  TileValidationContext context{setup, pattern, {}, {}, errors};
  for (const ProjectTile &tile : setup.tiles)
    validateTile(tile, context);
  return errors;
}

Json ProjectSetupModelClass::createBackendScript() const {
  const std::vector<std::string> errors = validate();
  if (!errors.empty())
    throw std::invalid_argument(errors.front());

  Json importStep = PipelineNs::createStep("import")->getConfigJson();
  replaceParameter(
      importStep, "path",
      fs::absolute(setup.inputDirectory).lexically_normal().string());
  if (!setup.filenameFormat.empty())
    replaceParameter(importStep, "format", setup.filenameFormat);

  Json phaseCorrelationStep =
      PipelineNs::createStep("phase-correlation")->getConfigJson();
  replaceParameter(phaseCorrelationStep, "overlap", setup.overlap);
  replaceParameter(phaseCorrelationStep, "niveau", setup.pyramidLevel);

  Json script = {{"project", createProjectJson(setup)},
                 {"pipeline",
                  {{"steps", Json::array({importStep, phaseCorrelationStep})}}},
                 {"run", createRunJson(setup)}};

  static_cast<void>(PipelineNs::buildPipeline(script));
  return script;
}

std::filesystem::path
ProjectSetupModelClass::scriptPathFor(const fs::path &outputDirectory,
                                      const std::string &projectName) {
  const fs::path normalizedOutput = fs::absolute(outputDirectory).lexically_normal();
  return normalizedOutput / (safeFileStem(projectName) + ".json");
}

fs::path ProjectSetupModelClass::writeBackendScript() const {
  const Json script = createBackendScript();
  const fs::path outputDirectory =
      fs::absolute(setup.outputDirectory).lexically_normal();
  fs::create_directories(outputDirectory);
  const fs::path destination = scriptPathFor(outputDirectory, setup.projectName);
  const fs::path temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary);
    output << script.dump(2) << '\n';
    if (!output)
      throw std::runtime_error(
          "Impossible d'écrire le fichier JSON temporaire.");
  }
  std::error_code errorCode;
  fs::rename(temporary, destination, errorCode);
  if (errorCode) {
    fs::remove(destination, errorCode);
    errorCode.clear();
    fs::rename(temporary, destination, errorCode);
  }
  if (errorCode)
    throw std::runtime_error("Impossible de finaliser le fichier JSON : " +
                             errorCode.message());
  return destination;
}

} // namespace FrontendNs
