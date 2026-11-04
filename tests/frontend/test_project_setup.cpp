#include "project_setup_model.h"

#include "core/pipeline/pipeline_builder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

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
  EXPECT_EQ(script.at("project").at("grid").at("tiles").size(), 6);
  EXPECT_EQ(importStep.at("name"), "import");
  EXPECT_EQ(parameterValue(importStep, "path"),
            fs::absolute(root / "input").string());
  EXPECT_EQ(parameterValue(importStep, "format"), R"(img_C(\d+)_R(\d+)\.tif)");
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

TEST_F(ProjectSetupTestClass, ReportsGridAndFilenameMismatchAfterMove) {
  model.swapTiles(0, 1);
  const auto errors = model.validate();
  EXPECT_TRUE(
      std::any_of(errors.begin(), errors.end(), [](const std::string &error) {
        return error.find("ne correspond pas") != std::string::npos;
      }));
}

TEST_F(ProjectSetupTestClass, AcceptsPositionsChosenInExistingGrid) {
  FrontendNs::ProjectSetup setup = model.getSetup();
  setup.filenameFormat.clear();
  std::swap(setup.tiles.front().path, setup.tiles.back().path);
  model.setSetup(setup);

  const FrontendNs::Json script = model.createBackendScript();
  EXPECT_NO_THROW(static_cast<void>(PipelineNs::buildPipeline(script)));
  const auto &tiles = script.at("project").at("grid").at("tiles");
  EXPECT_EQ(tiles.at(0).at("row"), 0);
  EXPECT_EQ(tiles.at(0).at("column"), 0);
  EXPECT_EQ(tiles.at(0).at("path"),
            fs::absolute(setup.tiles[0].path).string());
  EXPECT_EQ(tiles.back().at("row"), 2);
  EXPECT_EQ(tiles.back().at("column"), 1);
}

} // namespace
