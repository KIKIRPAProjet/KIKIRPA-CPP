#include "project_setup_model.h"

#include "core/pipeline/pipeline_builder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

const FrontendNs::Json &parameterValue(const FrontendNs::Json &step,
                                       const std::string &name) {
  for (const FrontendNs::Json &parameter : step.at("parameters")) {
    if (parameter.at("name") == name)
      return parameter.at("value");
  }
  throw std::out_of_range("missing parameter " + name);
}

class ProjectSetupTestClass : public ::testing::Test {

protected:
  void SetUp() override {
    root = fs::temp_directory_path() /
           ("kikirpa_frontend_" +
            std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::remove_all(root);
    fs::create_directories(root / "input");
    fs::create_directories(root / "output");
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 2; ++column) {
        std::ofstream(root / "input" /
                      ("img_C" + std::to_string(column) + "_R" +
                       std::to_string(row) + ".tif"))
            << "test";
      }
    }
    FrontendNs::ProjectSetup setup;
    setup.projectName = "Parcours Nathan";
    setup.inputDirectory = root / "input";
    setup.outputDirectory = root / "output";
    model.setSetup(setup);
    model.importTiles();
  }

  void TearDown() override { fs::remove_all(root); }

  fs::path root;
  FrontendNs::ProjectSetupModelClass model;
};

TEST_F(ProjectSetupTestClass, CreatesBackendAcceptedJsonForThreeByTwoGrid) {
  const FrontendNs::Json script = model.createBackendScript();
  const FrontendNs::Json &importStep = script.at("pipeline").at("steps").at(0);
  const FrontendNs::Json &phaseStep = script.at("pipeline").at("steps").at(1);
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  EXPECT_EQ(script.at("project").at("grid").at("rows"), 3);
  EXPECT_EQ(script.at("project").at("grid").at("columns"), 2);
  EXPECT_EQ(importStep.at("name"), "import");
  EXPECT_EQ(parameterValue(importStep, "path"),
            fs::absolute(root / "input").string());
  // La liste des tuiles du step import : fichier et case de chaque tuile ; l'id est calculé par import.
  const FrontendNs::Json &tiles = parameterValue(importStep, "tiles");
  ASSERT_EQ(tiles.size(), 6);
  EXPECT_FALSE(tiles.at(0).contains("id"));
  EXPECT_EQ(tiles.at(0).at("row"), 0);
  EXPECT_EQ(tiles.at(0).at("column"), 0);
  EXPECT_EQ(tiles.at(0).at("file"),
            fs::absolute(root / "input" / "img_C0_R0.tif").string());
  EXPECT_EQ(tiles.at(5).at("row"), 2);
  EXPECT_EQ(tiles.at(5).at("column"), 1);
  EXPECT_EQ(phaseStep.at("name"), "phase-correlation");
  EXPECT_DOUBLE_EQ(parameterValue(phaseStep, "overlap"), 0.2);
  EXPECT_EQ(parameterValue(phaseStep, "niveau"), 0);
  EXPECT_EQ(script.at("run").at("checkpointDirectory"),
            (fs::absolute(root / "output") / "checkpoints").string());
}

TEST_F(ProjectSetupTestClass, WritesJsonAtomically) {
  const fs::path scriptPath = model.writeBackendScript();
  EXPECT_TRUE(fs::is_regular_file(scriptPath));
  EXPECT_FALSE(fs::exists(scriptPath.string() + ".tmp"));
  EXPECT_NO_THROW(
      static_cast<void>(PipelineNs::buildPipelineFromFile(scriptPath)));
}

TEST_F(ProjectSetupTestClass, ReportsDuplicateTile) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.tiles[1].path = setup.tiles[0].path;
  model.setSetup(setup);
  const auto errors = model.validate();
  EXPECT_TRUE(
      std::any_of(errors.begin(), errors.end(), [](const std::string &error) {
        return error.find("dupliquée") != std::string::npos;
      }));
}

TEST_F(ProjectSetupTestClass, ReportsMissingFile) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  fs::remove(setup.tiles.back().path);
  model.setSetup(setup);
  const auto errors = model.validate();
  EXPECT_TRUE(
      std::any_of(errors.begin(), errors.end(), [](const std::string &error) {
        return error.find("Fichier absent") != std::string::npos;
      }));
}

TEST_F(ProjectSetupTestClass, ReportsIncompleteGrid) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.tiles.pop_back();
  model.setSetup(setup);
  const auto errors = model.validate();
  EXPECT_TRUE(
      std::any_of(errors.begin(), errors.end(), [](const std::string &error) {
        return error.find("exactement") != std::string::npos;
      }));
}

// La case retenue est celle de la grille, même si elle diffère des coordonnées écrites dans le nom.
TEST_F(ProjectSetupTestClass, AcceptsTilesMovedInTheGrid) {
  model.swapTiles(0, 1);
  EXPECT_TRUE(model.validate().empty());
}

TEST_F(ProjectSetupTestClass, AcceptsPositionsChosenInExistingGrid) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  std::swap(setup.tiles.front().path, setup.tiles.back().path);
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  const FrontendNs::Json &importStep = script.at("pipeline").at("steps").at(0);
  const auto &tiles = parameterValue(importStep, "tiles");
  EXPECT_EQ(tiles.at(0).at("row"), 0);
  EXPECT_EQ(tiles.at(0).at("column"), 0);
  EXPECT_EQ(tiles.at(0).at("file"),
            fs::absolute(setup.tiles[0].path).string());
  EXPECT_EQ(tiles.back().at("row"), 2);
  EXPECT_EQ(tiles.back().at("column"), 1);
}

// Des noms de fichiers libres (sans ligne ni colonne) sont acceptés : la case vient de la grille.
TEST_F(ProjectSetupTestClass, AcceptsFreeFileNames) {
  const fs::path freeDirectory = root / "free";
  fs::create_directories(freeDirectory);
  for (int index = 1; index <= 4; ++index)
    std::ofstream(freeDirectory / (std::to_string(index) + "_no filter f8 v30 20cm.tif")) << "test";
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.inputDirectory = freeDirectory;
  setup.gridRows = 2;
  setup.gridColumns = 2;
  model.setSetup(setup);
  model.importTiles();

  EXPECT_TRUE(model.validate().empty());
  const FrontendNs::Json script = model.createBackendScript();
  const FrontendNs::Json &importStep = script.at("pipeline").at("steps").at(0);
  EXPECT_EQ(parameterValue(importStep, "tiles").size(), 4);
}

// --- Méthodes choisies dans l'onglet Chaîne ---

std::vector<std::string> stepNames(const FrontendNs::Json &script) {
  std::vector<std::string> names;
  for (const FrontendNs::Json &step : script.at("pipeline").at("steps"))
    names.push_back(step.at("name").get<std::string>());
  return names;
}

// Les méthodes par défaut donnent le même script qu'avant leur ajout.
TEST_F(ProjectSetupTestClass, DefaultChoicesKeepThePreviousPipeline) {
  const std::vector<std::string> expected{"import", "phase-correlation", "absolute-pos", "intensity-correction",
                                          "graphcut", "fusion", "export"};
  EXPECT_EQ(stepNames(model.createBackendScript()), expected);
}

// Recouvrement réel et SIFT (avec son Vote 2D automatique) : les étapes sont ajoutées à leur place, et le backend
// accepte le script.
TEST_F(ProjectSetupTestClass, ChoicesAddOverlapEstimationMatcherAndVote) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.pipelineChoices.estimateOverlap = true;
  setup.pipelineChoices.matchingStep = "sift";
  setup.pipelineChoices.intensityMethod = "gain";
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  const std::vector<std::string> expected{"import", "overlap-estimation", "sift", "vote-2d", "absolute-pos",
                                          "intensity-correction", "graphcut", "fusion", "export"};
  EXPECT_EQ(stepNames(script), expected);
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  EXPECT_DOUBLE_EQ(parameterValue(script.at("pipeline").at("steps").at(2), "overlap"), 0.2);
  EXPECT_EQ(parameterValue(script.at("pipeline").at("steps").at(5), "method"), "gain");
}

// Recouvrements horizontal et vertical, et marge d'incertitude du rail : transmis à l'appariement. La marge de
// phase-correlation est en px du niveau analysé (ici niveau 1 : divisée par 2).
TEST_F(ProjectSetupTestClass, PassesBothOverlapsAndRailMarginToPhaseCorrelation) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.overlap = 0.21;
  setup.verticalOverlap = 0.26;
  setup.railMargin = 700.0;
  setup.pyramidLevel = 1;
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  const FrontendNs::Json &phaseStep = script.at("pipeline").at("steps").at(1);
  ASSERT_EQ(phaseStep.at("name"), "phase-correlation");
  EXPECT_DOUBLE_EQ(parameterValue(phaseStep, "overlap"), 0.21);
  EXPECT_DOUBLE_EQ(parameterValue(phaseStep, "verticalOverlap"), 0.26);
  EXPECT_DOUBLE_EQ(parameterValue(phaseStep, "marge"), 350.0);
}

TEST_F(ProjectSetupTestClass, PassesBothOverlapsAndRailMarginToOverlapEstimationAndSift) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.verticalOverlap = 0.26;
  setup.railMargin = 700.0;
  setup.pipelineChoices.estimateOverlap = true;
  setup.pipelineChoices.matchingStep = "sift";
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  for (const int index : {1, 2}) {
    const FrontendNs::Json &step = script.at("pipeline").at("steps").at(index);
    EXPECT_DOUBLE_EQ(parameterValue(step, "overlap"), 0.2) << step.at("name");
    EXPECT_DOUBLE_EQ(parameterValue(step, "verticalOverlap"), 0.26) << step.at("name");
    EXPECT_DOUBLE_EQ(parameterValue(step, "railMargin"), 700.0) << step.at("name");
  }
}

TEST_F(ProjectSetupTestClass, ReportsInvalidVerticalOverlap) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.verticalOverlap = 0.0;
  model.setSetup(setup);
  const auto errors = model.validate();
  EXPECT_TRUE(std::any_of(errors.begin(), errors.end(), [](const std::string &error) {
    return error.find("vertical") != std::string::npos;
  }));
}

// Le Vote 2D n'est jamais ajouté avec la corrélation de phase.
TEST_F(ProjectSetupTestClass, PhaseCorrelationNeverGetsVote2D) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.pipelineChoices.matchingStep = "phase-correlation";
  model.setSetup(setup);

  const std::vector<std::string> names = stepNames(model.createBackendScript());
  EXPECT_EQ(std::find(names.begin(), names.end(), "vote-2d"), names.end());
}

// LightGlue : Vote 2D automatique, et le modèle téléchargé dans le dossier de build par CMake.
TEST_F(ProjectSetupTestClass, LightGlueGetsVote2DAndTheBuildModel) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.pipelineChoices.matchingStep = "lightglue";
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  const std::vector<std::string> expected{"import", "lightglue", "vote-2d", "absolute-pos",
                                          "intensity-correction", "graphcut", "fusion", "export"};
  EXPECT_EQ(stepNames(script), expected);
  EXPECT_EQ(parameterValue(script.at("pipeline").at("steps").at(1), "modelPath"), FrontendNs::LIGHTGLUE_MODEL_PATH);
}

// Couture par programmation dynamique : remplace graphcut, et le backend accepte le script (territoires pour la fusion).
TEST_F(ProjectSetupTestClass, DynamicProgrammingReplacesGraphCut) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.pipelineChoices.seamStep = "dynamic-programming-dijkstra";
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  const std::vector<std::string> expected{"import", "phase-correlation", "absolute-pos", "intensity-correction",
                                          "dynamic-programming-dijkstra", "fusion", "export"};
  EXPECT_EQ(stepNames(script), expected);
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
}

// Le chemin du modèle fixé par l'interface (absolu, tiré du dossier de l'exécutable) est celui du script.
TEST_F(ProjectSetupTestClass, LightGlueUsesTheModelPathOfTheSetup) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.pipelineChoices.matchingStep = "lightglue";
  setup.lightGlueModelPath = root / "bin" / FrontendNs::LIGHTGLUE_MODEL_PATH;
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  const fs::path written(parameterValue(script.at("pipeline").at("steps").at(1), "modelPath").get<std::string>());
  EXPECT_TRUE(written.is_absolute());
  EXPECT_EQ(written, setup.lightGlueModelPath);
}

// Une méthode inconnue lue dans un projet reprend sa valeur par défaut ; les méthodes connues sont gardées.
TEST(PipelineChoicesTest, UnknownMethodsFallBackToTheDefaults) {
  FrontendNs::PipelineChoices read;
  read.estimateOverlap = true;
  read.matchingStep = "manual-matches";
  read.intensityMethod = "gain";
  read.seamStep = "voronoi";

  const FrontendNs::PipelineChoices valid = FrontendNs::validatedChoices(read);
  const FrontendNs::PipelineChoices defaults;
  EXPECT_TRUE(valid.estimateOverlap);
  EXPECT_EQ(valid.matchingStep, defaults.matchingStep);
  EXPECT_EQ(valid.intensityMethod, "gain");
  EXPECT_EQ(valid.seamStep, defaults.seamStep);

  FrontendNs::PipelineChoices known;
  known.matchingStep = "lightglue";
  known.seamStep = "dynamic-programming-dijkstra";
  EXPECT_EQ(FrontendNs::validatedChoices(known), known);
}

} // namespace
