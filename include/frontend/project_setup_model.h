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

// Méthode choisie pour chaque étape du pipeline (onglet Chaîne). Les noms sont ceux du registre des steps.
struct PipelineChoices {
  bool estimateOverlap = false;                    // ajoute overlap-estimation avant l'appariement
  std::string matchingStep = "phase-correlation";  // "phase-correlation", "sift" ou "lightglue"
  std::string intensityMethod = "gain-and-field";  // paramètre method de intensity-correction ("gain" ou "gain-and-field")
  std::string seamStep = "graphcut";               // "graphcut" ou "dynamic-programming-dijkstra" (territoires pour la fusion)

  bool operator==(const PipelineChoices &other) const {
    return estimateOverlap == other.estimateOverlap && matchingStep == other.matchingStep &&
           intensityMethod == other.intensityMethod && seamStep == other.seamStep;
  }
};

// Modèle de LightGlue, téléchargé par CMake dans le dossier de build (dossier de travail de l'application). Le matcher
// résout ce chemin relatif.
inline constexpr const char *LIGHTGLUE_MODEL_PATH = "models/raco_aliked_lightglue_pipeline_k1024.onnx";

// Le Vote 2D filtre des points appariés : ajouté d'office après SIFT et LightGlue, jamais après la corrélation de phase,
// qui donne peu de points par paire.
inline bool usesVote2D(const PipelineChoices &choices) {
  return choices.matchingStep != "phase-correlation";
}

// Noms des steps du script, dans l'ordre, pour ces choix.
std::vector<std::string> pipelineStepNames(const PipelineChoices &choices);

struct ProjectSetup {
  std::string projectName;
  std::filesystem::path inputDirectory;
  std::filesystem::path outputDirectory;
  int gridRows = 3;
  int gridColumns = 2;
  // Sert seulement à proposer une case à chaque tuile à l'import (importTiles) si
  // les noms la contiennent ; la case retenue est celle de la grille.
  std::string filenameFormat = R"(img_C(\d+)_R(\d+)\.tif)";
  double overlap = DEFAULT_OVERLAP;
  int pyramidLevel = 0;
  std::vector<ProjectTile> tiles;
  PipelineChoices pipelineChoices;
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

private:
  ProjectSetup setup;
};

} // namespace FrontendNs
