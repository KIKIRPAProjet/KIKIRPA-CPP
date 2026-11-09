// tests/core/test_pipeline_2x2.cpp
#include <gtest/gtest.h>

#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/pipeline.h"
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/data/tile.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

/**
 * @brief Environnement de test temporaire isolé (RAII).
 *        Crée deux sous-dossiers :
 *        - `images/` : contient les tuiles d'entrée synthétiques générées pour le test.
 *        - `output/` : répertoire cible recevant la mosaïque finale 'mosaic.tif'.
 *        Nettoie automatiquement l'ensemble de l'arborescence à la destruction.
 */
class TempWorkspace {
public:
    TempWorkspace() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("kikirpa_test_2x2_" + std::to_string(now));
        imagesDir = root / "images";
        outputDir = root / "output";
        fs::create_directories(imagesDir);
        fs::create_directories(outputDir);
    }

    ~TempWorkspace() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path root;
    fs::path imagesDir;
    fs::path outputDir;
};

// -----------------------------------------------------------------------------
// Définition de la géométrie de la grille 2x2 synthétique
// -----------------------------------------------------------------------------
constexpr int TILE_WIDTH = 400;                     // Largeur d'une tuile individuelle en pixels
constexpr int TILE_HEIGHT = 300;                    // Hauteur d'une tuile individuelle en pixels
constexpr int OVERLAP_X = 80;                       // Recouvrement horizontal : 20% de 400 px
constexpr int OVERLAP_Y = 60;                       // Recouvrement vertical : 20% de 300 px
constexpr int SHIFT_X = TILE_WIDTH - OVERLAP_X;     // Décalage horizontal entre colonnes voisines = 320 px
constexpr int SHIFT_Y = TILE_HEIGHT - OVERLAP_Y;   // Décalage vertical entre lignes voisines = 240 px
constexpr int CANVAS_WIDTH = SHIFT_X + TILE_WIDTH;  // Largeur totale de la mosaïque = 720 px
constexpr int CANVAS_HEIGHT = SHIFT_Y + TILE_HEIGHT;// Hauteur totale de la mosaïque = 540 px

/**
 * @brief Génère une scène texturée synthétique continue et en découpe 4 tuiles avec un recouvrement exact de 20%.
 *
 * Utilise un bruit uniforme lissé par filtre gaussien pour offrir une texture continue et riche en
 * fréquences, indispensable pour que la corrélation de phase (phase-correlation) puisse converger
 * avec précision sans ambiguïté de périodicité.
 */
void createSyntheticTiles2x2(const fs::path& imagesDir) {
    // 1. Génération d'une grande image continue aléatoire 32-bits flottants
    cv::Mat canvas32f(CANVAS_HEIGHT, CANVAS_WIDTH, CV_32FC3);
    cv::RNG rng(4242);
    rng.fill(canvas32f, cv::RNG::UNIFORM, 0.0, 1.0);

    // 2. Lissage gaussien pour introduire une cohérence spatiale et des structures de contours
    cv::GaussianBlur(canvas32f, canvas32f, cv::Size(), 2.0);

    // 3. Normalisation dans la dynamique 16-bits non-signée (typiquement [2000, 60000])
    cv::normalize(canvas32f, canvas32f, 2000.0, 60000.0, cv::NORM_MINMAX);

    cv::Mat canvas16u;
    canvas32f.convertTo(canvas16u, CV_16UC3);

    // 4. Découpage et sauvegarde des 4 tuiles selon le schéma de nommage KIKIRPA standard (rLL_cCC)
    // Tuile (ligne 0, colonne 0)
    cv::Mat t00 = canvas16u(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c00.tif").string(), t00);

    // Tuile (ligne 0, colonne 1) - décalée à droite de SHIFT_X
    cv::Mat t01 = canvas16u(cv::Rect(SHIFT_X, 0, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r00_c01.tif").string(), t01);

    // Tuile (ligne 1, colonne 0) - décalée vers le bas de SHIFT_Y
    cv::Mat t10 = canvas16u(cv::Rect(0, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c00.tif").string(), t10);

    // Tuile (ligne 1, colonne 1) - décalée en diagonale de (SHIFT_X, SHIFT_Y)
    cv::Mat t11 = canvas16u(cv::Rect(SHIFT_X, SHIFT_Y, TILE_WIDTH, TILE_HEIGHT));
    cv::imwrite((imagesDir / "img_r01_c01.tif").string(), t11);
}

/**
 * @brief Construit le script JSON décrivant la chaîne complète de bout en bout :
 *        import -> phase-correlation -> absolute-pos -> intensity-correction -> graphcut -> fusion -> export
 */
Json build2x2PipelineScript(const fs::path& imagesDir, const fs::path& outputDir) {
    return {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", imagesDir.string()}},
                        {{"valueType", "string"}, {"name", "format"}, {"value", ".*_r(?=\\d+_c(\\d+)\\.tiff?$)(\\d+)_c\\d+\\.tiff?"}}
                    })}
                },
                {
                    {"name", "phase-correlation"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "niveau"}, {"value", 0}},
                        {{"valueType", "float"}, {"name", "overlap"}, {"value", 0.2f}}
                    })}
                },
                {
                    {"name", "absolute-pos"}
                },
                {
                    {"name", "intensity-correction"}
                },
                {
                    {"name", "graphcut"},
                    {"parameters", Json::array({
                        {{"valueType", "float"}, {"name", "scale"}, {"value", 0.5f}}
                    })}
                },
                {
                    {"name", "fusion"},
                    {"parameters", Json::array({
                        {{"valueType", "int"}, {"name", "blockSize"}, {"value", 512}}
                    })}
                },
                {
                    {"name", "export"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "outputDirectory"}, {"value", outputDir.string()}}
                    })}
                }
            })}
        }}
    };
}

} // namespace

/**
 * @brief Vérifie le rejet strict des scripts invalides lors de la compilation du pipeline.
 */
TEST(Pipeline2x2Test, RejectsInvalidConfigurationWithSpecificCause) {
    // Cas 1 : Nom de step inexistant dans le registre
    const Json scriptInconnu = {
        {"pipeline", {
            {"steps", Json::array({
                {{"name", "step_inexistant"}}
            })}
        }}
    };
    EXPECT_THROW({
        try {
            PipelineNs::buildPipeline(scriptInconnu);
        } catch (const std::invalid_argument& ex) {
            EXPECT_NE(std::string(ex.what()).find("unknown step"), std::string::npos);
            throw;
        }
    }, std::invalid_argument);

    // Cas 2 : Rupture du contrat de dépendances entre steps :
    // 'graphcut' requiert l'entrée 'positions', mais aucun step de positionnement n'a été inséré.
    const Json scriptChaineInvalide = {
        {"pipeline", {
            {"steps", Json::array({
                {
                    {"name", "import"},
                    {"parameters", Json::array({
                        {{"valueType", "string"}, {"name", "path"}, {"value", "dummy/"}}
                    })}
                },
                {{"name", "graphcut"}} // Erreur : 'positions' non encore disponible
            })}
        }}
    };
    EXPECT_THROW({
        try {
            PipelineNs::buildPipeline(scriptChaineInvalide);
        } catch (const std::invalid_argument& ex) {
            EXPECT_NE(std::string(ex.what()).find("positions"), std::string::npos);
            throw;
        }
    }, std::invalid_argument);
}

/**
 * @brief Test d'intégration de bout en bout sur une grille réelle 2×2 d'images :
 *  - Exécute sans aucun bouchon (mock) l'intégralité du pipeline depuis le système de fichiers.
 *  - Vérifie la présence et la cohérence de chaque structure de données intermédiaire dans le DataStore.
 *  - Valide la création effective de l'image finale 'mosaic.tif' et sa conformité (dimensions, format 16 bits).
 */
TEST(Pipeline2x2Test, ExecutesFullChainOnReal2x2DatasetUntilExport) {
    TempWorkspace ws;
    createSyntheticTiles2x2(ws.imagesDir);

    const Json script = build2x2PipelineScript(ws.imagesDir, ws.outputDir);

    // Compilation et validation du graphe d'exécution
    PipelineNs::PipelineClass pipeline = PipelineNs::buildPipeline(script);

    // Exécution du pipeline complet avec capture des traces de log
    std::vector<std::string> logs;
    const bool success = pipeline.run([&logs](const std::string& msg) {
        logs.push_back(msg);
        std::cout << "    [pipeline 2x2] " << msg << std::endl;
    });

    EXPECT_TRUE(success);

    const auto& store = pipeline.getDataStore();

    // 1. Étape 'import' : les 4 tuiles doivent être chargées
    ASSERT_TRUE(store.has("tiles"));
    const auto& tiles = store.get<DataNs::TileList>("tiles");
    EXPECT_EQ(tiles.size(), 4u);

    // 2. Étape 'phase-correlation' : 4 paires de tuiles adjacentes appariées (2 horizontales, 2 verticales)
    ASSERT_TRUE(store.has("matches"));
    const auto& matches = store.get<std::vector<DataNs::PairMatches>>("matches");
    EXPECT_EQ(matches.size(), 4u);

    // 3. Étape 'absolute-pos' : 4 positions absolues optimisées dans le repère de la mosaïque
    ASSERT_TRUE(store.has("positions"));
    const auto& positions = store.get<std::vector<DataNs::TilePositionData>>("positions");
    EXPECT_EQ(positions.size(), 4u);

    // 4. Étape 'intensity-correction' : 4 gains calculés pour équilibrer la luminosité
    ASSERT_TRUE(store.has("intensityCorrection"));
    const auto& ic = store.get<DataNs::IntensityCorrection>("intensityCorrection");
    EXPECT_EQ(ic.tileGains.size(), 4u);

    // 5. Étape 'graphcut' : 4 territoires de découpe polygonaux sans chevauchement
    ASSERT_TRUE(store.has("territories"));
    const auto& territories = store.get<std::vector<DataNs::TileTerritory>>("territories");
    EXPECT_EQ(territories.size(), 4u);

    // 6. Étape 'fusion' : blocs composites réguliers générés en mémoire
    ASSERT_TRUE(store.has("compositeBlocks"));
    const auto& blocks = store.get<DataNs::CompositeBlockList>("compositeBlocks");
    EXPECT_FALSE(blocks.empty());

    // 7. Étape 'export' : écriture du fichier TIFF tuilé sur le disque
    const fs::path expectedMosaicPath = ws.outputDir / "mosaic.tif";
    EXPECT_TRUE(fs::exists(expectedMosaicPath));
    EXPECT_GT(fs::file_size(expectedMosaicPath), 0u);

    // Contrôle direct de l'image finale exportée via OpenCV
    cv::Mat exportedImage = cv::imread(expectedMosaicPath.string(), cv::IMREAD_UNCHANGED);
    ASSERT_FALSE(exportedImage.empty());
    EXPECT_EQ(exportedImage.type(), CV_16UC3); // 16 bits non-signé 3 canaux
    EXPECT_GE(exportedImage.cols, CANVAS_WIDTH - 20);
    EXPECT_GE(exportedImage.rows, CANVAS_HEIGHT - 20);
}
