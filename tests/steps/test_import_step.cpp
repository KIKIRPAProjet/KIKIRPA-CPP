// tests/steps/test_import_step.cpp
// Step import : identifiant, fichier et case (ligne, colonne) de chaque tuile, avec la liste `tiles` ou avec `format`.
#include "steps/import_step.h"

#include "core/data/tile.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace {

    namespace fs = std::filesystem;
    using Json = nlohmann::json;

    constexpr int TILE_SIZE = 16;
    constexpr int GREY_LEVEL = 100;

    struct TestDirectory {
        fs::path path;
        TestDirectory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("import-step-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~TestDirectory() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    };

    void writeImage(const fs::path& path) {
        cv::imwrite(path.string(), cv::Mat(TILE_SIZE, TILE_SIZE, CV_8UC1, cv::Scalar(GREY_LEVEL)));
    }

    Json listedTile(int tileId, const std::string& file, int row, int column) {
        return {{"id", tileId}, {"file", file}, {"row", row}, {"column", column}};
    }

    // Step import configuré avec un dossier et une liste de tuiles (vide : mode format).
    StepsNs::ImportStepClass makeImportStep(const fs::path& directory, const Json& tiles) {
        StepsNs::ImportStepClass step;
        step.importConfig({{"parameters", {{{"valueType", "string"}, {"name", "path"}, {"value", directory.string()}},
                                           {{"valueType", "json"}, {"name", "tiles"}, {"value", tiles}}}}});
        return step;
    }

    // Quatre images aux noms libres, comme les vraies données ; la liste les place dans une grille 2 x 2.
    Json writeFreeNamedGrid(const fs::path& directory) {
        for (const char* name : {"1_no filter.tif", "2_no filter.tif", "3_no filter.tif", "4_no filter.tif"})
            writeImage(directory / name);
        return Json::array({listedTile(0, "2_no filter.tif", 0, 0), listedTile(1, "3_no filter.tif", 0, 1),
                            listedTile(2, "1_no filter.tif", 1, 0), listedTile(3, "4_no filter.tif", 1, 1)});
    }

}

TEST(ImportStepTest, ListGivesIdFileAndGridCellOfEachTile) {
    const TestDirectory directory;
    StepsNs::ImportStepClass step = makeImportStep(directory.path, writeFreeNamedGrid(directory.path));

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const DataNs::TileList& tiles = step.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 4U);
    EXPECT_EQ(tiles[2]->getId(), 2);
    EXPECT_EQ(tiles[2]->getSource().filename(), "1_no filter.tif");
    EXPECT_EQ(tiles[2]->getGridRow(), 1);
    EXPECT_EQ(tiles[2]->getGridColumn(), 0);
    EXPECT_EQ(tiles[0]->getRight(), tiles[1].get());
    EXPECT_EQ(tiles[0]->getBottom(), tiles[2].get());
    EXPECT_EQ(tiles[3]->getLeft(), tiles[2].get());
}

// L'ordre des entrées de la liste ne compte pas : les tuiles sont rangées ligne par ligne, puis colonne par colonne.
TEST(ImportStepTest, ListOrderDoesNotMatter) {
    const TestDirectory directory;
    Json tiles = writeFreeNamedGrid(directory.path);
    std::swap(tiles[0], tiles[3]);
    StepsNs::ImportStepClass step = makeImportStep(directory.path, tiles);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    EXPECT_EQ(step.getOutput().tiles[0]->getSource().filename(), "2_no filter.tif");
    EXPECT_EQ(step.getOutput().tiles[3]->getSource().filename(), "4_no filter.tif");
}

TEST(ImportStepTest, FormatGivesGridCellFromFileName) {
    const TestDirectory directory;
    writeImage(directory.path / "img_C0_R0.tif");
    writeImage(directory.path / "img_C1_R0.tif");
    StepsNs::ImportStepClass step = makeImportStep(directory.path, Json::array());

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const DataNs::TileList& tiles = step.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 2U);
    EXPECT_EQ(tiles[1]->getGridRow(), 0);
    EXPECT_EQ(tiles[1]->getGridColumn(), 1);
    EXPECT_EQ(tiles[0]->getRight(), tiles[1].get());
}

TEST(ImportStepTest, RejectsAnIdThatIsNotTheRankInRowOrder) {
    const TestDirectory directory;
    Json tiles = writeFreeNamedGrid(directory.path);
    tiles[1]["id"] = 3;
    tiles[3]["id"] = 1;
    StepsNs::ImportStepClass step = makeImportStep(directory.path, tiles);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult()["error"].get<std::string>().find("must have id 1"), std::string::npos);
}

TEST(ImportStepTest, RejectsTwoTilesInTheSameCell) {
    const TestDirectory directory;
    Json tiles = writeFreeNamedGrid(directory.path);
    tiles[1]["column"] = 0;
    StepsNs::ImportStepClass step = makeImportStep(directory.path, tiles);

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult()["error"].get<std::string>().find("share the grid cell"), std::string::npos);
}

TEST(ImportStepTest, RejectsAMissingFileOrAMalformedEntry) {
    const TestDirectory directory;
    Json tiles = writeFreeNamedGrid(directory.path);
    tiles[2]["file"] = "absent.tif";
    StepsNs::ImportStepClass missingFileStep = makeImportStep(directory.path, tiles);
    EXPECT_FALSE(missingFileStep.execute());
    EXPECT_NE(missingFileStep.getResult()["error"].get<std::string>().find("missing file"), std::string::npos);

    tiles = writeFreeNamedGrid(directory.path);
    tiles[0].erase("row");
    StepsNs::ImportStepClass malformedStep = makeImportStep(directory.path, tiles);
    EXPECT_FALSE(malformedStep.execute());
    EXPECT_NE(malformedStep.getResult()["error"].get<std::string>().find("tiles[0]"), std::string::npos);
}

// L'empreinte de reprise suit les fichiers de la liste : en remplacer un périme le checkpoint.
TEST(ImportStepTest, FingerprintFollowsListedFiles) {
    const TestDirectory directory;
    Json tiles = writeFreeNamedGrid(directory.path);
    const Json before = makeImportStep(directory.path, tiles).externalInputsFingerprint();
    ASSERT_TRUE(before.is_array());
    EXPECT_EQ(before.size(), 4U);

    cv::imwrite((directory.path / "5_no filter.tif").string(), cv::Mat(TILE_SIZE * 2, TILE_SIZE, CV_8UC1, cv::Scalar(0)));
    tiles[3]["file"] = "5_no filter.tif";
    EXPECT_NE(makeImportStep(directory.path, tiles).externalInputsFingerprint(), before);
}
