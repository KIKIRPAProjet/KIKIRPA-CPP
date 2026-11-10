#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace FrontendNs {

using Json = nlohmann::json;

inline constexpr int MAX_GRID_DIMENSION = 20;
inline constexpr int MAX_PYRAMID_LEVEL = 5;
inline constexpr double DEFAULT_OVERLAP = 0.2;

struct ProjectTile {
  std::filesystem::path path;
  int row = 0;
  int column = 0;
};

struct ProjectSetup {
  std::string projectName;
  std::filesystem::path inputDirectory;
  std::filesystem::path outputDirectory;
  int gridRows = 3;
  int gridColumns = 2;
  std::string filenameFormat = R"(img_C(\d+)_R(\d+)\.tif)";
  double overlap = DEFAULT_OVERLAP;
  int pyramidLevel = 0;
  std::vector<ProjectTile> tiles;
};

// Modèle indépendant de l'affichage pour que le parcours puisse être validé
// sans ouvrir Qt.
class ProjectSetupModelClass {

public:
  const ProjectSetup &getSetup() const { return setup; }
  void setSetup(ProjectSetup newSetup);

  // Liste les TIFF du dossier et les place ligne par ligne dans les cases
  // disponibles.
  void importTiles();
  void resizeGrid(int rows, int columns);
  void swapTiles(std::size_t firstIndex, std::size_t secondIndex);

  // Retourne toutes les anomalies afin que l'interface puisse les présenter en
  // une fois.
  std::vector<std::string> validate() const;

  // Produit le script commun, puis le fait relire par le vrai PipelineBuilder
  // du backend.
  Json createBackendScript() const;
  std::filesystem::path writeBackendScript() const;

  // Calcule le chemin déterministe du script JSON généré pour un projet.
  static std::filesystem::path
  scriptPathFor(const std::filesystem::path &outputDirectory,
                const std::string &projectName);

private:
  ProjectSetup setup;
};

} // namespace FrontendNs
