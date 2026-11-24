// tests/steps/test_import_step.cpp
// Step import : identifiant, fichier et case (ligne, colonne) de chaque tuile, avec la liste `tiles` ou avec `format`.
#include "steps/import_step.h"

#include "core/data/tile.h"
#include "core/io/pyramid_cache.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace {

    namespace fs = std::filesystem;
    using Json = nlohmann::json;

    constexpr int TILE_SIZE = 16;
    constexpr int GREY_LEVEL = 100;
    constexpr std::chrono::hours ONE_DAY{24};   // recul de la date d'une source « plus ancienne »

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

    // Entrée de la liste `tiles` : le fichier et sa case. L'identifiant n'y est pas, import le calcule.
    Json listedTile(const std::string& file, int row, int column) {
        return {{"file", file}, {"row", row}, {"column", column}};
    }

    // Step import configuré avec un dossier, une liste de tuiles (vide : cases lues dans les noms) et un nombre de
    // colonnes (0 : non donné).
    StepsNs::ImportStepClass makeImportStep(const fs::path& directory, const Json& tiles, int columns = 0) {
        StepsNs::ImportStepClass step;
        step.importConfig({{"parameters", {{{"valueType", "string"}, {"name", "path"}, {"value", directory.string()}},
                                           {{"valueType", "int"}, {"name", "columns"}, {"value", columns}},
                                           {{"valueType", "json"}, {"name", "tiles"}, {"value", tiles}}}}});
        return step;
    }

    // Quatre images aux noms libres, comme les vraies données ; la liste les place dans une grille 2 x 2.
    Json writeFreeNamedGrid(const fs::path& directory) {
        for (const char* name : {"1_no filter.tif", "2_no filter.tif", "3_no filter.tif", "4_no filter.tif"})
            writeImage(directory / name);
        return Json::array({listedTile("2_no filter.tif", 0, 0), listedTile("3_no filter.tif", 0, 1),
                            listedTile("1_no filter.tif", 1, 0), listedTile("4_no filter.tif", 1, 1)});
    }

}

TEST(ImportStepTest, ListGivesIdFileAndGridCellOfEachTile) {
    const TestDirectory directory;
    StepsNs::ImportStepClass step = makeImportStep(directory.path, writeFreeNamedGrid(directory.path));

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    EXPECT_EQ(step.getResult()["placement"], "list");
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
    EXPECT_EQ(step.getResult()["placement"], "fileName");
}

// Sans liste, des noms qui ne donnent pas la case ne sont pas une erreur : les images, triées par nom, remplissent la
// grille ligne par ligne sur `columns` colonnes.
TEST(ImportStepTest, WithoutListOrMatchingNamesPlacesImagesInNameOrder) {
    const TestDirectory directory;
    for (const char* name : {"3_no filter.tif", "1_no filter.tif", "4_no filter.tiff", "2_no filter.TIF"})
        writeImage(directory.path / name);
    StepsNs::ImportStepClass step = makeImportStep(directory.path, Json::array(), 2);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    EXPECT_EQ(step.getResult()["placement"], "nameOrder");
    const DataNs::TileList& tiles = step.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 4U);
    EXPECT_EQ(tiles[0]->getSource().filename(), "1_no filter.tif");
    EXPECT_EQ(tiles[1]->getSource().filename(), "2_no filter.TIF");
    EXPECT_EQ(tiles[2]->getSource().filename(), "3_no filter.tif");
    EXPECT_EQ(tiles[2]->getGridRow(), 1);
    EXPECT_EQ(tiles[2]->getGridColumn(), 0);
    EXPECT_EQ(tiles[0]->getBottom(), tiles[2].get());
}

// Des noms qui donnent la case gardent la priorité sur l'ordre des noms, même si `columns` est donné.
TEST(ImportStepTest, MatchingNamesWinOverNameOrder) {
    const TestDirectory directory;
    writeImage(directory.path / "img_C0_R1.tif");
    writeImage(directory.path / "img_C0_R0.tif");
    StepsNs::ImportStepClass step = makeImportStep(directory.path, Json::array(), 2);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    EXPECT_EQ(step.getResult()["placement"], "fileName");
    EXPECT_EQ(step.getOutput().tiles[1]->getGridRow(), 1);
    EXPECT_EQ(step.getOutput().tiles[1]->getGridColumn(), 0);
}

// Sans liste ni nom qui donne la case, il faut le nombre de colonnes : le message dit quoi donner.
TEST(ImportStepTest, AsksForColumnsWhenNamesGiveNoCell) {
    const TestDirectory directory;
    writeImage(directory.path / "a.tif");
    StepsNs::ImportStepClass step = makeImportStep(directory.path, Json::array());

    EXPECT_FALSE(step.execute());
    EXPECT_NE(step.getResult()["error"].get<std::string>().find("'columns'"), std::string::npos);
}

// Deux images de même nom (a.tif et a.tiff) ont chacune leur TIFF pyramidal : son nom porte l'empreinte du chemin
// complet de l'image (core/io/pyramid_cache.h).
TEST(ImportStepTest, GivesEachTileItsOwnPyramidEvenWithTheSameFileName) {
    const TestDirectory directory;
    writeImage(directory.path / "a.tif");
    writeImage(directory.path / "a.tiff");
    StepsNs::ImportStepClass step =
        makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0), listedTile("a.tiff", 0, 1)}));

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const DataNs::TileList& tiles = step.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 2U);
    const fs::path cacheDirectory = IONs::pyramidCacheDirectory(directory.path);
    EXPECT_EQ(tiles[0]->getTiff(), IONs::pyramidCachePath(cacheDirectory, directory.path / "a.tif"));
    EXPECT_EQ(tiles[1]->getTiff(), IONs::pyramidCachePath(cacheDirectory, directory.path / "a.tiff"));
    EXPECT_NE(tiles[0]->getTiff(), tiles[1]->getTiff());
    EXPECT_TRUE(fs::is_regular_file(tiles[0]->getTiff()));
    EXPECT_TRUE(fs::is_regular_file(tiles[1]->getTiff()));
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

// Une pyramide déjà existante et à jour est réutilisée sans être régénérée.
TEST(ImportStepTest, ReusesExistingValidPyramidWithoutRegeneration) {
    const TestDirectory directory;
    writeImage(directory.path / "a.tif");
    StepsNs::ImportStepClass step1 =
        makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(step1.execute());
    const auto pyramidPath = step1.getOutput().tiles[0]->getTiff();
    ASSERT_TRUE(fs::is_regular_file(pyramidPath));
    const auto initialTime = fs::last_write_time(pyramidPath);

    // Second import : le cache valide doit être réutilisé
    StepsNs::ImportStepClass step2 =
        makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(step2.execute());
    EXPECT_EQ(fs::last_write_time(pyramidPath), initialTime);
}

// Si l'image source a été modifiée après la pyramide, celle-ci doit être régénérée.
TEST(ImportStepTest, RegeneratesPyramidWhenSourceImageIsModified) {
    const TestDirectory directory;
    const fs::path sourcePath = directory.path / "a.tif";
    writeImage(sourcePath);
    StepsNs::ImportStepClass step1 =
        makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(step1.execute());
    const auto pyramidPath = step1.getOutput().tiles[0]->getTiff();
    ASSERT_TRUE(fs::is_regular_file(pyramidPath));

    // On simule une pyramide ancienne (10 secondes dans le passé)
    const auto oldPyramidTime = fs::last_write_time(pyramidPath) - std::chrono::seconds(10);
    fs::last_write_time(pyramidPath, oldPyramidTime);

    // Et une image source plus récente que cette ancienne pyramide
    const auto newerSourceTime = oldPyramidTime + std::chrono::seconds(5);
    writeImage(sourcePath);
    fs::last_write_time(sourcePath, newerSourceTime);

    StepsNs::ImportStepClass step2 =
        makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(step2.execute());
    EXPECT_GT(fs::last_write_time(pyramidPath), oldPyramidTime);
    EXPECT_GE(fs::last_write_time(pyramidPath), newerSourceTime);
}

namespace {

// Pixel de la pyramide d'une tuile, au coin haut gauche de la pleine résolution.
std::uint8_t firstPyramidPixel(const DataNs::TileClass& tile) {
    const IONs::PyramidalTiffReaderClass reader(tile.getTiff());
    cv::Mat fullResolution;
    std::string errorMessage;
    EXPECT_TRUE(reader.readLevel(0, fullResolution, errorMessage)) << errorMessage;
    return fullResolution.empty() ? std::uint8_t{0} : fullResolution.at<std::uint8_t>(0, 0);
}

constexpr std::uint8_t FIRST_GREY = 42;
constexpr std::uint8_t SECOND_GREY = 142;

void writeGreyImage(const fs::path& path, std::uint8_t grey) {
    ASSERT_TRUE(IONs::writeImage(path, cv::Mat(TILE_SIZE, TILE_SIZE, CV_8UC1, cv::Scalar(grey))));
}

}

// Deux fichiers de même nom sans extension mais d'images différentes (photo.tif et photo.tiff) :
// ils obtiennent chacun leur propre pyramide sans mélanger leur contenu.
TEST(ImportStepTest, DistinctContentForSameStemFilesDoesNotMixPyramids) {
    const TestDirectory directory;
    writeGreyImage(directory.path / "photo.tif", FIRST_GREY);
    writeGreyImage(directory.path / "photo.tiff", SECOND_GREY);

    StepsNs::ImportStepClass step =
        makeImportStep(directory.path, Json::array({listedTile("photo.tif", 0, 0), listedTile("photo.tiff", 0, 1)}));
    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const DataNs::TileList& tiles = step.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 2U);
    EXPECT_NE(tiles[0]->getTiff(), tiles[1]->getTiff());
    EXPECT_EQ(firstPyramidPixel(*tiles[0]), FIRST_GREY);
    EXPECT_EQ(firstPyramidPixel(*tiles[1]), SECOND_GREY);
}

// Scénario d'Eva (#159) : photo.tif et photo.tiff importées, puis permutées dans la grille. Chaque case doit
// recharger les pixels de sa nouvelle source, et non le cache de l'image qui l'occupait avant.
TEST(ImportStepTest, SwappedSameStemFilesReloadTheirOwnPixels) {
    const TestDirectory directory;
    writeGreyImage(directory.path / "photo.tif", FIRST_GREY);
    writeGreyImage(directory.path / "photo.tiff", SECOND_GREY);
    StepsNs::ImportStepClass first =
        makeImportStep(directory.path, Json::array({listedTile("photo.tif", 0, 0), listedTile("photo.tiff", 0, 1)}));
    ASSERT_TRUE(first.execute()) << first.getResult().dump();

    StepsNs::ImportStepClass swapped =
        makeImportStep(directory.path, Json::array({listedTile("photo.tiff", 0, 0), listedTile("photo.tif", 0, 1)}));
    ASSERT_TRUE(swapped.execute()) << swapped.getResult().dump();
    const DataNs::TileList& tiles = swapped.getOutput().tiles;
    ASSERT_EQ(tiles.size(), 2U);
    EXPECT_EQ(firstPyramidPixel(*tiles[0]), SECOND_GREY);   // case (0, 0) : photo.tiff
    EXPECT_EQ(firstPyramidPixel(*tiles[1]), FIRST_GREY);    // case (0, 1) : photo.tif
}

// Une source remplacée par un fichier plus ancien que sa pyramide (copie qui garde sa date, restauration d'une
// sauvegarde) invalide quand même le cache : la date doit être identique, pas seulement antérieure.
TEST(ImportStepTest, RegeneratesPyramidWhenSourceIsReplacedByAnOlderFile) {
    const TestDirectory directory;
    const fs::path sourcePath = directory.path / "a.tif";
    writeGreyImage(sourcePath, FIRST_GREY);
    StepsNs::ImportStepClass first = makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(first.execute()) << first.getResult().dump();
    const fs::path pyramidPath = first.getOutput().tiles[0]->getTiff();

    writeGreyImage(sourcePath, SECOND_GREY);
    fs::last_write_time(sourcePath, fs::last_write_time(pyramidPath) - ONE_DAY);

    StepsNs::ImportStepClass second = makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(second.execute()) << second.getResult().dump();
    EXPECT_EQ(second.getOutput().tiles[0]->getTiff(), pyramidPath);
    EXPECT_EQ(firstPyramidPixel(*second.getOutput().tiles[0]), SECOND_GREY);
}

// Pyramide réutilisée : la fiche garde les dimensions et le type de l'image, lus dans la pyramide.
TEST(ImportStepTest, ReusedPyramidKeepsTheImageShape) {
    const TestDirectory directory;
    ASSERT_TRUE(IONs::writeImage(directory.path / "a.tif", cv::Mat(TILE_SIZE, 2 * TILE_SIZE, CV_16UC3, cv::Scalar::all(GREY_LEVEL))));
    StepsNs::ImportStepClass first = makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(first.execute()) << first.getResult().dump();
    StepsNs::ImportStepClass second = makeImportStep(directory.path, Json::array({listedTile("a.tif", 0, 0)}));
    ASSERT_TRUE(second.execute()) << second.getResult().dump();

    const DataNs::TileClass& tile = *second.getOutput().tiles[0];
    EXPECT_EQ(tile.getLevel(0).width, 2 * TILE_SIZE);
    EXPECT_EQ(tile.getLevel(0).height, TILE_SIZE);
    EXPECT_EQ(tile.getType(), CV_16UC3);
}

// L'empreinte suit la PRÉSENCE de la pyramide, pas sa date : la tâche de fond de l'interface les réécrit à l'identique
// avec une date neuve, et le checkpoint de l'import ne doit pas en être périmé. En revanche, un cache supprimé doit
// périmer le checkpoint, pour que l'import le reconstruise.
TEST(ImportStepTest, FingerprintFollowsThePyramidPresenceButNotItsDate) {
    const TestDirectory directory;
    const Json tiles = writeFreeNamedGrid(directory.path);
    StepsNs::ImportStepClass step = makeImportStep(directory.path, tiles);
    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const Json initial = step.externalInputsFingerprint();
    ASSERT_FALSE(initial.is_null());

    const fs::path pyramid = step.getOutput().tiles.front()->getTiff();
    ASSERT_TRUE(fs::exists(pyramid));
    fs::last_write_time(pyramid, fs::last_write_time(pyramid) + ONE_DAY);
    EXPECT_EQ(step.externalInputsFingerprint(), initial);

    ASSERT_TRUE(fs::remove(pyramid));
    EXPECT_NE(step.externalInputsFingerprint(), initial);
}

// Le ménage de fin d'import retire les pyramides dont la source a disparu, et le compte dans son résultat.
TEST(ImportStepTest, RemovesOrphanPyramidsAtTheEndOfTheRun) {
    const TestDirectory directory;
    const Json tiles = writeFreeNamedGrid(directory.path);
    StepsNs::ImportStepClass firstRun = makeImportStep(directory.path, tiles);
    ASSERT_TRUE(firstRun.execute()) << firstRun.getResult().dump();
    EXPECT_EQ(firstRun.getResult().value("removedCachedFiles", -1), 0);
    const fs::path orphanPyramid = firstRun.getOutput().tiles.back()->getTiff();

    // La dernière image quitte le projet : sa pyramide n'a plus de source.
    Json remaining = Json::array();
    for (std::size_t index = 0; index + 1 < tiles.size(); ++index) remaining.push_back(tiles.at(index));
    ASSERT_TRUE(fs::remove(directory.path / tiles.back().at("file").get<std::string>()));

    StepsNs::ImportStepClass secondRun = makeImportStep(directory.path, remaining);
    ASSERT_TRUE(secondRun.execute()) << secondRun.getResult().dump();
    EXPECT_EQ(secondRun.getResult().value("removedCachedFiles", -1), 2);   // la pyramide et sa provenance
    EXPECT_FALSE(fs::exists(orphanPyramid));
}
