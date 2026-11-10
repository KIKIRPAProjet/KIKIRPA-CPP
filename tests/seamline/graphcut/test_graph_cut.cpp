// tests/seamline/graphcut/test_graph_cut.cpp
// Tests unitaires de la coupe de graphe (GraphCutSeamFinderClass) et de son step (GraphCutStepClass).
// Les tuiles sont des images synthétiques écrites dans un dossier temporaire : aucun fichier externe n'est nécessaire.
#include "algorithms/seamline/graph_cut/graph_cut_seam_finder.h"
#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"
#include "steps/graph_cut_step.h"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

    // Tuile type : 120 x 90 pixels. Deux tuiles décalées de 90 pixels se chevauchent sur 30 pixels.
    const cv::Size TILE_SIZE(120, 90);
    constexpr int OVERLAP_STEP_X = 90;
    constexpr int OVERLAP_STEP_Y = 65;

    // Facteur de réduction courant des tests, dans ]0, 1].
    constexpr double DEFAULT_SCALE = 0.5;

    // Valeurs des canaux de la scène synthétique : motif dense, déterministe, qui varie dans les deux directions
    // (le coût couleur de la coupe a besoin de contraste pour choisir une frontière).
    constexpr int PATTERN_MODULO = 251;
    constexpr int PATTERN_GAIN = 200;

    // Description d'une tuile synthétique.
    struct TileSpec {
        std::string id;        // "rLL_cCC", doit correspondre au nom du fichier
        cv::Point position;    // coin supérieur gauche dans la mosaïque
        double exposureGain;   // simule une exposition différente d'une tuile à l'autre
    };

    // Valeur d'un canal de la scène au point (x, y), en coordonnées de la mosaïque.
    ushort getSceneValue(int x, int y, int channel) {
        const int pattern = (x * (7 + 3 * channel) + y * (11 + 5 * channel) + (x * y) % 97 * 13) % PATTERN_MODULO;
        return static_cast<ushort>(pattern * PATTERN_GAIN + x * 5 + y * 3);
    }

    // Image 16 bits de la scène vue par une tuile : deux tuiles qui se chevauchent montrent les mêmes pixels de la scène.
    cv::Mat makeTileImage(const TileSpec& spec) {
        cv::Mat image(TILE_SIZE, CV_16UC3);
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x)
                for (int channel = 0; channel < 3; ++channel)
                    image.at<cv::Vec3w>(y, x)[channel] =
                        cv::saturate_cast<ushort>(getSceneValue(spec.position.x + x, spec.position.y + y, channel) * spec.exposureGain);
        return image;
    }

    // Prépare les tuiles à l'aide de ImagePretreatmentClass.
    bool prepareTiles(const StepsNs::GraphCutInput& input, GraphCutNs::PreprocessedTiles& tiles) {
        GraphCutNs::ImagePretreatmentClass imagePretreatment;
        return imagePretreatment.processImages(input.positions, input.tilePaths, input.scale, tiles) >= 0;
    }

    // Quatre tuiles en grille 2 x 2, avec des recouvrements horizontaux, verticaux et un point de jonction.
    std::vector<TileSpec> makeGridSpecs() {
        return {{"r00_c00", cv::Point(0, 0), 1.00},
                {"r00_c01", cv::Point(OVERLAP_STEP_X + 2, 3), 1.05},
                {"r01_c00", cv::Point(2, OVERLAP_STEP_Y), 0.95},
                {"r01_c01", cv::Point(OVERLAP_STEP_X + 5, OVERLAP_STEP_Y + 8), 1.10}};
    }

    // Deux tuiles côte à côte, qui se chevauchent.
    std::vector<TileSpec> makePairSpecs() {
        return {{"r00_c00", cv::Point(0, 0), 1.00}, {"r00_c01", cv::Point(OVERLAP_STEP_X, 4), 1.05}};
    }

    // Matrice 8 x 8 : la moitié gauche appartient à la tuile 0, la moitié droite à la tuile 1.
    constexpr int SPLIT_SIZE = 8;
    cv::Mat makeSplitIndexMatrix() {
        cv::Mat indexMatrix(SPLIT_SIZE, SPLIT_SIZE, CV_8U, cv::Scalar(GraphCutNs::NO_TILE));
        indexMatrix(cv::Rect(0, 0, SPLIT_SIZE / 2, SPLIT_SIZE)).setTo(0);
        indexMatrix(cv::Rect(SPLIT_SIZE / 2, 0, SPLIT_SIZE / 2, SPLIT_SIZE)).setTo(1);
        return indexMatrix;
    }

    // Tailles et coins des deux tuiles de makeSplitIndexMatrix().
    const std::vector<cv::Size> SPLIT_TILE_SIZES{cv::Size(SPLIT_SIZE / 2, SPLIT_SIZE), cv::Size(SPLIT_SIZE / 2, SPLIT_SIZE)};
    const std::vector<cv::Point> SPLIT_TILE_CORNERS{cv::Point(0, 0), cv::Point(SPLIT_SIZE / 2, 0)};

    bool contains(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }

    // Fixture : un dossier temporaire propre à chaque test, qui reçoit les images synthétiques.
    class TileFilesTest : public ::testing::Test {

        protected:
            void SetUp() override {
                const ::testing::TestInfo* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
                std::string directoryName = std::string("kikirpa_") + testInfo->test_suite_name() + "_" + testInfo->name();
                std::replace(directoryName.begin(), directoryName.end(), '/', '_');   
                tileDirectory = std::filesystem::temp_directory_path() / directoryName;
                std::filesystem::remove_all(tileDirectory);
                std::filesystem::create_directories(tileDirectory);
            }

            void TearDown() override {
                std::filesystem::remove_all(tileDirectory);
            }

            // Écrit les images des tuiles et renvoie l'entrée du step correspondante (chemins et positions).
            StepsNs::GraphCutInput writeTiles(const std::vector<TileSpec>& specs, double scale) const {
                StepsNs::GraphCutInput input;
                input.scale = scale;
                for (const TileSpec& spec : specs) {
                    const std::string path = (tileDirectory / ("tile_" + spec.id + ".tif")).string();
                    const bool isWritten = cv::imwrite(path, makeTileImage(spec));
                    EXPECT_TRUE(isWritten) << "Unable to write " << path;
                    input.tilePaths.push_back(path);
                    input.positions.push_back({spec.id, cv::Point2d(spec.position.x, spec.position.y)});
                }
                return input;
            }

            std::filesystem::path tileDirectory;

    };

    using GraphCutStepTest = TileFilesTest;
    using GraphCutSeamFinderTest = TileFilesTest;

    class GraphCutScaleTest : public TileFilesTest, public ::testing::WithParamInterface<double> {};

}

// ---------------------------------------------------------------------------------------------------------------------
// Step : contrôles des entrées
// ---------------------------------------------------------------------------------------------------------------------

TEST(GraphCutStepChecksTest, ExecuteFailsWhenNoTileIsProvided) {
    StepsNs::GraphCutStepClass step{StepsNs::GraphCutInput{}};

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "No tile provided"));
}

TEST(GraphCutStepChecksTest, ExecuteFailsWhenPositionCountDiffersFromTileCount) {
    StepsNs::GraphCutInput input;
    input.tilePaths = {"a_r00_c00.tif", "a_r00_c01.tif"};
    input.positions = {{"r00_c00", cv::Point2d(0, 0)}};
    StepsNs::GraphCutStepClass step(input);

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    const std::string error = step.getResult()["error"];
    EXPECT_TRUE(contains(error, "number of positions (1)")) << error;
    EXPECT_TRUE(contains(error, "number of tile paths (2)")) << error;
}

TEST(GraphCutStepChecksTest, ExecuteFailsWhenTooManyTilesAreProvided) {
    constexpr std::size_t TILE_COUNT = 255;   // GraphCutNs::NO_TILE vaut généralement 255
    StepsNs::GraphCutInput input;
    input.tilePaths.assign(TILE_COUNT, "a_r00_c00.tif");
    input.positions.assign(TILE_COUNT, {"r00_c00", cv::Point2d(0, 0)});
    StepsNs::GraphCutStepClass step(input);

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "got 255, max 254")) << step.getResult()["error"];
}

class GraphCutInvalidScaleTest : public ::testing::TestWithParam<double> {};

TEST_P(GraphCutInvalidScaleTest, ExecuteFailsWhenScaleIsOutOfRange) {
    StepsNs::GraphCutInput input;
    input.tilePaths = {"a_r00_c00.tif"};
    input.positions = {{"r00_c00", cv::Point2d(0, 0)}};
    input.scale = GetParam();
    StepsNs::GraphCutStepClass step(input);

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "Reduction scale must be")) << step.getResult()["error"];
}

INSTANTIATE_TEST_SUITE_P(OutOfRangeScales, GraphCutInvalidScaleTest, ::testing::Values(0.0, -0.5, 1.5));

// ---------------------------------------------------------------------------------------------------------------------
// Step : exécution sur des tuiles synthétiques
// ---------------------------------------------------------------------------------------------------------------------

TEST_F(GraphCutStepTest, ExecuteFailsWhenATileFileCannotBeRead) {
    StepsNs::GraphCutInput input = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    input.tilePaths[1] = (tileDirectory / "missing_r00_c01.tif").string();
    StepsNs::GraphCutStepClass step(input);

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "Image pretreatment error")) << step.getResult()["error"];
}

TEST_F(GraphCutStepTest, ExecuteFailsWhenATilePositionIsMissing) {
    StepsNs::GraphCutInput input = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    input.positions[1].tileId = "r09_c09";   
    StepsNs::GraphCutStepClass step(input);

    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "Image pretreatment error")) << step.getResult()["error"];
}

TEST_F(GraphCutStepTest, ExecuteFailsWhenTileNameIsInvalid) {
    StepsNs::GraphCutInput input = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    
    // On renomme le premier fichier sur le disque pour qu'il soit lisible (imread fonctionne) 
    // mais que son nom échoue à l'analyse de l'expression régulière de parseTileName.
    const std::string badPath = (tileDirectory / "invalid_name.tif").string();
    std::filesystem::rename(input.tilePaths[0], badPath);
    input.tilePaths[0] = badPath;
    
    StepsNs::GraphCutStepClass step(input);
    const bool isSuccessful = step.execute();

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(step.getResult()["error"], "Tile file name must end with")) 
        << step.getResult()["error"];
}

TEST_F(GraphCutStepTest, ExecuteReturnsOneTerritoryPerTileInTileOrder) {
    StepsNs::GraphCutStepClass step(writeTiles(makeGridSpecs(), DEFAULT_SCALE));

    const bool isSuccessful = step.execute();

    ASSERT_TRUE(isSuccessful) << step.getResult().dump();
    const std::vector<DataNs::TileTerritory>& territories = step.getOutput().territories;
    ASSERT_EQ(territories.size(), 4u);
    for (std::size_t tileIndex = 0; tileIndex < territories.size(); ++tileIndex) {
        EXPECT_EQ(territories[tileIndex].tileIndex, static_cast<int>(tileIndex));
        EXPECT_FALSE(territories[tileIndex].polygons.empty()) << "tile " << tileIndex;
    }
    EXPECT_EQ(step.getResult()["territories"], 4);
}

TEST_F(GraphCutStepTest, ExecuteStoresTerritoriesInTheDataStore) {
    StepNs::DataStoreClass dataStore;
    StepsNs::GraphCutStepClass step(writeTiles(makePairSpecs(), DEFAULT_SCALE));
    step.setDataStore(dataStore);

    const bool isSuccessful = step.execute();

    ASSERT_TRUE(isSuccessful) << step.getResult().dump();
    ASSERT_TRUE(dataStore.has("territories"));
    EXPECT_EQ(dataStore.get<std::vector<DataNs::TileTerritory>>("territories").size(), 2u);
}

TEST_F(GraphCutStepTest, ExecuteReadsTilesAndPositionsFromTheDataStore) {
    const StepsNs::GraphCutInput written = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    DataNs::TileList tiles;
    for (std::size_t tileIndex = 0; tileIndex < written.tilePaths.size(); ++tileIndex)
        tiles.push_back(std::make_shared<DataNs::TileClass>(static_cast<int>(tileIndex), written.tilePaths[tileIndex], "",
                                                            TILE_SIZE.width, TILE_SIZE.height, CV_16UC3));
    StepNs::DataStoreClass dataStore;
    dataStore.add("tiles", tiles);
    dataStore.add("positions", written.positions);
    StepsNs::GraphCutStepClass step{StepsNs::GraphCutInput{}};   
    step.setDataStore(dataStore);

    const bool isSuccessful = step.execute();

    ASSERT_TRUE(isSuccessful) << step.getResult().dump();
    EXPECT_EQ(step.getOutput().territories.size(), 2u);
}

// ---------------------------------------------------------------------------------------------------------------------
// Coupe de graphe : carte de provenance
// ---------------------------------------------------------------------------------------------------------------------

TEST_P(GraphCutScaleTest, TerritoriesTileTheMosaicExactly) {
    const StepsNs::GraphCutInput input = writeTiles(makeGridSpecs(), GetParam());
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    ASSERT_TRUE(graphCut.computeSeamline(input.tilePaths, fullRes, reducedGrid, indexMatrix))
        << graphCut.getErrorMessage();

    std::vector<cv::Rect> tileRects;
    for (std::size_t i = 0; i < tiles.tileSizes.size(); ++i) {
        tileRects.emplace_back(tiles.originalCorners[i], tiles.tileSizes[i]);
    }
    const std::vector<DataNs::TileTerritory> territories = graphCut.extractPolygons(indexMatrix, tileRects);

    // Vérification basique des aires (pavage)
    EXPECT_EQ(territories.size(), 4u);
    for (const auto& territory : territories) {
        EXPECT_FALSE(territory.polygons.empty());
    }

    // Vérification de la conservation du centre
    for (std::size_t i = 0; i < tiles.tileSizes.size(); ++i) {
        cv::Point center = tiles.originalCorners[i] + cv::Point(tiles.tileSizes[i].width / 2, tiles.tileSizes[i].height / 2);
        EXPECT_EQ(indexMatrix.at<uchar>(center.y, center.x), static_cast<uchar>(i))
            << "La tuile " << i << " a perdu son centre.";
    }
}

TEST_P(GraphCutScaleTest, EveryPixelGoesToATileThatCoversIt) {
    const StepsNs::GraphCutInput input = writeTiles(makeGridSpecs(), GetParam());
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    ASSERT_TRUE(graphCut.computeSeamline(input.tilePaths, fullRes, reducedGrid, indexMatrix))
        << graphCut.getErrorMessage();

    int wrongPixelCount = 0;
    for (int y = 0; y < indexMatrix.rows; ++y) {
        for (int x = 0; x < indexMatrix.cols; ++x) {
            const uchar assignedTile = indexMatrix.at<uchar>(y, x);
            bool isCovered = false;
            for (std::size_t tileIndex = 0; tileIndex < tiles.tileSizes.size(); ++tileIndex)
                isCovered = isCovered || cv::Rect(tiles.originalCorners[tileIndex], tiles.tileSizes[tileIndex]).contains(cv::Point(x, y));
            const bool isAssignedToCoveringTile =
                assignedTile != GraphCutNs::NO_TILE &&
                cv::Rect(tiles.originalCorners[assignedTile], tiles.tileSizes[assignedTile]).contains(cv::Point(x, y));
            if (isCovered != isAssignedToCoveringTile)
                ++wrongPixelCount;
        }
    }
    EXPECT_EQ(wrongPixelCount, 0);
}

INSTANTIATE_TEST_SUITE_P(Scales, GraphCutScaleTest, ::testing::Values(1.0, 0.5, 0.3));

TEST_F(GraphCutSeamFinderTest, ComputeSeamlineLeavesPixelsCoveredByNoTileUnassigned) {
    // Deux tuiles en diagonale, sans recouvrement : le coin supérieur droit de la mosaïque n'est couvert par aucune tuile.
    const cv::Point farCorner(OVERLAP_STEP_X + TILE_SIZE.width, OVERLAP_STEP_Y + TILE_SIZE.height);
    const StepsNs::GraphCutInput input = writeTiles({{"r00_c00", cv::Point(0, 0), 1.0}, {"r01_c01", farCorner, 1.0}}, DEFAULT_SCALE);
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    // On sabote le chemin de la tuile isolée. Si l'algorithme tente de la lire, la coupe échouera.
    std::vector<std::string> hackedPaths = input.tilePaths;
    hackedPaths[1] = (tileDirectory / "this_file_does_not_exist.tif").string();

    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    // Le succès garantit que la tuile isolée n'a jamais été ouverte (car NOT_USED)
    ASSERT_TRUE(graphCut.computeSeamline(hackedPaths, fullRes, reducedGrid, indexMatrix))
        << graphCut.getErrorMessage();

    EXPECT_EQ(indexMatrix.at<uchar>(0, 0), 0);
    EXPECT_EQ(indexMatrix.at<uchar>(farCorner.y, farCorner.x), 1);
    EXPECT_EQ(indexMatrix.at<uchar>(0, indexMatrix.cols - 1), GraphCutNs::NO_TILE);
    EXPECT_EQ(indexMatrix.at<uchar>(indexMatrix.rows - 1, 0), GraphCutNs::NO_TILE);
}

TEST_F(GraphCutSeamFinderTest, ComputeSeamlineGivesTheWholeMosaicToASingleTile) {
    const StepsNs::GraphCutInput input = writeTiles({{"r00_c00", cv::Point(0, 0), 1.0}}, DEFAULT_SCALE);
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    ASSERT_TRUE(graphCut.computeSeamline(input.tilePaths, fullRes, reducedGrid, indexMatrix))
        << graphCut.getErrorMessage();

    EXPECT_EQ(indexMatrix.size(), TILE_SIZE);
    EXPECT_EQ(cv::countNonZero(indexMatrix), 0); 
}

// ---------------------------------------------------------------------------------------------------------------------
// Coupe de graphe : cas d'erreur de computeSeamline
// ---------------------------------------------------------------------------------------------------------------------

TEST(GraphCutClassErrorTest, ComputeSeamlineFailsWithoutTiles) {
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes;
    GraphCutNs::ReducedGrid reducedGrid;
    reducedGrid.scale = DEFAULT_SCALE;

    const bool isSuccessful = graphCut.computeSeamline({}, fullRes, reducedGrid, indexMatrix);

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(graphCut.getErrorMessage(), "Tile count must be between 1 and 254")) << graphCut.getErrorMessage();
}

TEST(GraphCutClassErrorTest, ComputeSeamlineFailsWhenTileCountReachesTheReservedValue) {
    constexpr std::size_t TILE_COUNT = 255;
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    const std::vector<std::string> paths(TILE_COUNT, "a_r00_c00.tif");
    
    GraphCutNs::FullResolutionGeometry fullRes;
    fullRes.tileSizes.assign(TILE_COUNT, TILE_SIZE);
    fullRes.originalCorners.assign(TILE_COUNT, cv::Point(0, 0));
    
    GraphCutNs::ReducedGrid reducedGrid;
    reducedGrid.scale = DEFAULT_SCALE;
    reducedGrid.cornersSmall.assign(TILE_COUNT, cv::Point(0, 0));
    reducedGrid.masksUmatSmall.resize(TILE_COUNT);
    
    cv::Mat indexMatrix;

    const bool isSuccessful = graphCut.computeSeamline(paths, fullRes, reducedGrid, indexMatrix);

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(graphCut.getErrorMessage(), "got 255")) << graphCut.getErrorMessage();
}

TEST(GraphCutClassErrorTest, ComputeSeamlineFailsWhenListsDoNotMatchTheTileCount) {
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    
    GraphCutNs::FullResolutionGeometry fullRes;
    fullRes.tileSizes.assign(2, TILE_SIZE);
    fullRes.originalCorners.assign(2, cv::Point(0, 0));
    
    GraphCutNs::ReducedGrid reducedGrid;
    reducedGrid.scale = DEFAULT_SCALE;
    reducedGrid.cornersSmall.assign(2, cv::Point(0, 0));
    reducedGrid.masksUmatSmall.resize(2);

    cv::Mat indexMatrix;

    const bool isSuccessful = graphCut.computeSeamline({"only_one_r00_c00.tif"}, fullRes, reducedGrid, indexMatrix);

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(graphCut.getErrorMessage(), "one entry per tile size")) << graphCut.getErrorMessage();
}

TEST_F(GraphCutSeamFinderTest, ComputeSeamlineNamesTheUnreadableTileInTheErrorMessage) {
    StepsNs::GraphCutInput input = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    const std::string missingPath = (tileDirectory / "gone_r00_c01.tif").string();
    input.tilePaths[1] = missingPath;   
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    const bool isSuccessful = graphCut.computeSeamline(input.tilePaths, fullRes, reducedGrid, indexMatrix);

    EXPECT_FALSE(isSuccessful);
    EXPECT_TRUE(contains(graphCut.getErrorMessage(), missingPath)) << graphCut.getErrorMessage();
}

TEST_F(GraphCutSeamFinderTest, ErrorMessageIsClearedByASuccessfulCall) {
    const StepsNs::GraphCutInput input = writeTiles(makePairSpecs(), DEFAULT_SCALE);
    GraphCutNs::PreprocessedTiles tiles;
    ASSERT_TRUE(prepareTiles(input, tiles));
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    cv::Mat indexMatrix;
    
    GraphCutNs::FullResolutionGeometry emptyFullRes;
    GraphCutNs::ReducedGrid emptyReducedGrid;
    emptyReducedGrid.scale = DEFAULT_SCALE;

    ASSERT_FALSE(graphCut.computeSeamline({}, emptyFullRes, emptyReducedGrid, indexMatrix));
    ASSERT_FALSE(graphCut.getErrorMessage().empty());

    GraphCutNs::FullResolutionGeometry fullRes{tiles.tileSizes, tiles.originalCorners};
    GraphCutNs::ReducedGrid reducedGrid{input.scale, tiles.cornersSmall, tiles.masksUmatSmall};

    const bool isSuccessful = graphCut.computeSeamline(input.tilePaths, fullRes, reducedGrid, indexMatrix);

    EXPECT_TRUE(isSuccessful);
    EXPECT_TRUE(graphCut.getErrorMessage().empty());
}

// ---------------------------------------------------------------------------------------------------------------------
// Polygones des territoires : matrices construites à la main, sans image
// ---------------------------------------------------------------------------------------------------------------------

TEST(GraphCutPolygonsTest, ExtractPolygonsGivesOnePolygonPerTileForASplitMosaic) {
    GraphCutNs::GraphCutSeamFinderClass graphCut;

    std::vector<cv::Rect> splitRects = {
        cv::Rect(SPLIT_TILE_CORNERS[0], SPLIT_TILE_SIZES[0]),
        cv::Rect(SPLIT_TILE_CORNERS[1], SPLIT_TILE_SIZES[1])
    };

    const std::vector<DataNs::TileTerritory> territories = graphCut.extractPolygons(makeSplitIndexMatrix(), splitRects);

    ASSERT_EQ(territories.size(), 2u);
    for (int tileIndex = 0; tileIndex < 2; ++tileIndex) {
        EXPECT_EQ(territories[tileIndex].tileIndex, tileIndex);
        EXPECT_EQ(territories[tileIndex].polygons.size(), 1u);
        EXPECT_TRUE(territories[tileIndex].holes[0].empty());
    }
}

TEST(GraphCutPolygonsTest, ExtractPolygonsReportsAHoleInsideATerritory) {
    constexpr int SIZE = 9;
    constexpr int HOLE_SIZE = 3;
    cv::Mat indexMatrix(SIZE, SIZE, CV_8U, cv::Scalar(0));
    indexMatrix(cv::Rect(HOLE_SIZE, HOLE_SIZE, HOLE_SIZE, HOLE_SIZE)).setTo(1);   
    
    GraphCutNs::GraphCutSeamFinderClass graphCut;

    std::vector<cv::Rect> splitRects = {
        cv::Rect(0, 0, SIZE, SIZE),
        cv::Rect(HOLE_SIZE, HOLE_SIZE, HOLE_SIZE, HOLE_SIZE)
    };

    const std::vector<DataNs::TileTerritory> territories = graphCut.extractPolygons(indexMatrix, splitRects);

    ASSERT_EQ(territories.size(), 2u);
    ASSERT_EQ(territories[0].polygons.size(), 1u);
    EXPECT_EQ(territories[0].holes[0].size(), 1u);
    ASSERT_EQ(territories[1].polygons.size(), 1u);
    EXPECT_TRUE(territories[1].holes[0].empty());
}

TEST(GraphCutPolygonsTest, ExtractPolygonsGivesAnEmptyTerritoryToATileAbsentFromTheMap) {
    GraphCutNs::GraphCutSeamFinderClass graphCut;

    std::vector<cv::Rect> splitRects = {
        cv::Rect(SPLIT_TILE_CORNERS[0], SPLIT_TILE_SIZES[0]),
        cv::Rect(SPLIT_TILE_CORNERS[1], SPLIT_TILE_SIZES[1]),
        cv::Rect(0, 0, SPLIT_SIZE, SPLIT_SIZE)
    };

    const std::vector<DataNs::TileTerritory> territories = graphCut.extractPolygons(makeSplitIndexMatrix(), splitRects);

    ASSERT_EQ(territories.size(), 3u);
    EXPECT_EQ(territories[2].tileIndex, 2);
    EXPECT_TRUE(territories[2].polygons.empty());
}

TEST(GraphCutPolygonsTest, ExtractPolygonsReturnsNothingForZeroTiles) {
    GraphCutNs::GraphCutSeamFinderClass graphCut;
    std::vector<cv::Rect> emptyRects;
    EXPECT_TRUE(graphCut.extractPolygons(makeSplitIndexMatrix(), emptyRects).empty());
}